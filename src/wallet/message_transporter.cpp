// Copyright (c) 2019, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "message_transporter.h"
#include <boost/format.hpp>
#include "wallet_errors.h"
#include <algorithm>
#include <selamc/base64.h>

#undef SELAM_DEFAULT_LOG_CATEGORY
#define SELAM_DEFAULT_LOG_CATEGORY "wallet.mms"
#define PYBITMESSAGE_DEFAULT_API_PORT 8442

using namespace std::literals;

namespace mms
{

namespace bitmessage_rpc
{

  struct message_info
  {
    uint32_t encodingType;
    std::string toAddress;
    uint32_t read;
    std::string msgid;
    std::string message;
    std::string fromAddress;
    std::string receivedTime;
    std::string subject;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(encodingType)
      KV_SERIALIZE(toAddress)
      KV_SERIALIZE(read)
      KV_SERIALIZE(msgid)
      KV_SERIALIZE(message);
      KV_SERIALIZE(fromAddress)
      KV_SERIALIZE(receivedTime)
      KV_SERIALIZE(subject)
    END_KV_SERIALIZE_MAP()
  };

  struct inbox_messages_response
  {
    std::vector<message_info> inboxMessages;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(inboxMessages)
    END_KV_SERIALIZE_MAP()
  };

}

void message_transporter::set_options(const std::string &bitmessage_address, const epee::wipeable_string &bitmessage_login)
{
  auto [proto, host, port, uri] = m_http_client.parse_url(bitmessage_address);
  if (port == 0) port = PYBITMESSAGE_DEFAULT_API_PORT;

  m_http_client.set_base_url(proto + "://" + host + ":" + std::to_string(port) + uri);
  if (!bitmessage_login.empty()) {
    auto login = bitmessage_login.view();
    auto colon = login.find(':');
    m_http_client.set_auth(login.substr(0, colon), login.substr(colon == std::string_view::npos ? login.size() : colon + 1));
  }
  m_http_client.set_timeout(15s);
}

bool message_transporter::receive_messages(const std::vector<std::string> &destination_transport_addresses,
                                           std::vector<transport_message> &messages)
{
  // The message body of the Bitmessage message is basically the transport message, as JSON (and nothing more).
  // Weeding out other, non-MMS messages is done in a simple way: If it deserializes without error, it's an MMS message
  // That JSON is Base64-encoded by the MMS because the Monero epee JSON serializer does not escape anything and happily
  // includes even 0 (NUL) in strings, which might confuse Bitmessage or at least display confusingly in the client.
  // There is yet another Base64-encoding of course as part of the Bitmessage API for the message body parameter
  // The Bitmessage API call "getAllInboxMessages" gives back a JSON array with all the messages (despite using
  // XML-RPC for the calls, and not JSON-RPC ...)
  m_run.store(true, std::memory_order_relaxed);
  std::string request;
  start_xml_rpc_cmd(request, "getAllInboxMessages");
  end_xml_rpc_cmd(request);
  std::string answer;
  post_request(request, answer);

  std::string json = get_str_between_tags(answer, "<string>", "</string>");
  bitmessage_rpc::inbox_messages_response bitmessage_res;
  if (!epee::serialization::load_t_from_json(bitmessage_res, json))
  {
    MERROR("Failed to deserialize messages");
    return true;
  }
  size_t size = bitmessage_res.inboxMessages.size();
  messages.clear();

  for (size_t i = 0; i < size; ++i)
  {
    if (!m_run.load(std::memory_order_relaxed))
    {
      // Stop was called, don't waste time processing any more messages
      return false;
    }
    const bitmessage_rpc::message_info &message_info = bitmessage_res.inboxMessages[i];
    if (std::find(destination_transport_addresses.begin(), destination_transport_addresses.end(), message_info.toAddress) != destination_transport_addresses.end())
    {
      try
      {
        // First Base64-decoding: The message body is Base64 in the Bitmessage API
        if (!selamc::is_base64(message_info.message))
          throw std::runtime_error{"not base64"};
        std::string message_body = selamc::from_base64(message_info.message);
        // Second Base64-decoding: The MMS uses Base64 to hide non-textual data in its JSON from Bitmessage
        // (WTF?)
        if (!selamc::is_base64(message_body))
          throw std::runtime_error{"decoded base64 does not contain more base64"};
        transport_message message{};
        if (!epee::serialization::load_t_from_json(message, selamc::from_base64(message_body)))
          throw std::runtime_error{"epee json-to-type deserialization failed"};

        message.transport_id = message_info.msgid;
        messages.push_back(std::move(message));
      }
      catch(const std::exception& e)
      {
        MERROR("Failed to deserialize message: " << e.what());
      }
    }
  }

  return true;
}

void message_transporter::send_message(const transport_message &message)
{
  // <toAddress> <fromAddress> <subject> <message> [encodingType [TTL]]
  std::string request;
  start_xml_rpc_cmd(request, "sendMessage");
  add_xml_rpc_string_param(request, message.destination_transport_address);
  add_xml_rpc_string_param(request, message.source_transport_address);
  add_xml_rpc_base64_param(request, message.subject);
  // See comment in "receive_message" about (shitty) reason for (double-)Base64 encoding
  add_xml_rpc_base64_param(request, selamc::to_base64(epee::serialization::store_t_to_json(message)));
  add_xml_rpc_integer_param(request, 2);
  end_xml_rpc_cmd(request);
  std::string answer;
  post_request(request, answer);
}

void message_transporter::delete_message(const std::string &transport_id)
{
  std::string request;
  start_xml_rpc_cmd(request, "trashMessage");
  add_xml_rpc_string_param(request, transport_id);
  end_xml_rpc_cmd(request);
  std::string answer;
  post_request(request, answer);
}

// Deterministically derive a new transport address from 'seed' (the 10-hex-digits auto-config
// token will be used) and set it up for sending and receiving
// In a first attempt a normal Bitmessage address was used here, but it turned out the
// key exchange necessary to put it into service could take a long time or even did not
// work out at all sometimes. Also there were problems when deleting those temporary
// addresses again after auto-config. Now a chan is used which avoids all these drawbacks
// quite nicely.
std::string message_transporter::derive_transport_address(const std::string &seed)
{
  // Don't use the seed directly as chan name; that would be too dangerous, e.g. in the
  // case of a PyBitmessage instance used by multiple unrelated people
  // If an auto-config token gets hashed in another context use different salt instead of "chan"
  std::string salted_seed = seed + "chan";
  std::string chan_name = tools::type_to_hex(crypto::cn_fast_hash(salted_seed.data(), salted_seed.size()));

  // Calculate the Bitmessage address that the chan will get for being able to
  // use 'joinChain', as 'createChan' will fail and not tell the address if the chan
  // already exists (which it can if all auto-config participants share a PyBitmessage
  // instance). 'joinChan' will also fail in that case, but that won't matter.
  std::string request;
  start_xml_rpc_cmd(request, "getDeterministicAddress");
  add_xml_rpc_base64_param(request, chan_name);
  add_xml_rpc_integer_param(request, 4);  // addressVersionNumber
  add_xml_rpc_integer_param(request, 1);  // streamNumber
  end_xml_rpc_cmd(request);
  std::string answer;
  post_request(request, answer);
  std::string address = get_str_between_tags(answer, "<string>", "</string>");

  start_xml_rpc_cmd(request, "joinChan");
  add_xml_rpc_base64_param(request, chan_name);
  add_xml_rpc_string_param(request, address);
  end_xml_rpc_cmd(request);
  post_request(request, answer);
  return address;
}

void message_transporter::delete_transport_address(const std::string &transport_address)
{
  std::string request;
  start_xml_rpc_cmd(request, "leaveChan");
  add_xml_rpc_string_param(request, transport_address);
  end_xml_rpc_cmd(request);
  std::string answer;
  post_request(request, answer);
}

void message_transporter::post_request(const std::string &request, std::string &answer)
{
  try {
    auto res = m_http_client.post("", request, {{"Content-Type", "application/xml; charset=utf-8"}});
    answer = res.text;
  } catch (const std::exception& e) {
    LOG_ERROR("POST request to Bitmessage failed: " << e.what());
    THROW_WALLET_EXCEPTION(tools::error::no_connection_to_bitmessage, m_http_client.get_base_url());
  }

  std::string string_value = get_str_between_tags(answer, "<string>", "</string>");
  if ((string_value.find("API Error") == 0) || (string_value.find("RPC ") == 0))
  {
    if ((string_value.find("API Error 0021") == 0) && (request.find("joinChan") != std::string::npos))
    {
      // Error that occurs if one tries to join an already joined chan, which can happen
      // if several auto-config participants share one PyBitmessage instance: As a little
      // hack simply ignore the error. (A clean solution would be to check for the chan
      // with 'listAddresses2', but parsing the returned array is much more complicated.)
    }
    else if ((string_value.find("API Error 0013") == 0) && (request.find("leaveChan") != std::string::npos))
    {
      // Error that occurs if one tries to leave an already left / deleted chan, which can happen
      // if several auto-config participants share one PyBitmessage instance: Also ignore.
    }
    else
    {
      THROW_WALLET_EXCEPTION(tools::error::bitmessage_api_error, string_value);
    }
  }
}

// Pick some string between two delimiters
// When parsing the XML returned by PyBitmessage, don't bother to fully parse it but as a little hack rely on the
// fact that e.g. a single string returned will be, however deeply nested in "<params><param><value>...", delivered
// between the very first "<string>" and "</string>" tags to be found in the XML
std::string message_transporter::get_str_between_tags(const std::string &s, const std::string &start_delim, const std::string &stop_delim)
{
  size_t first_delim_pos = s.find(start_delim);
  if (first_delim_pos != std::string::npos)
  {
    size_t end_pos_of_first_delim = first_delim_pos + start_delim.length();
    size_t last_delim_pos = s.find(stop_delim);
    if (last_delim_pos != std::string::npos)
    {
      return s.substr(end_pos_of_first_delim, last_delim_pos - end_pos_of_first_delim);
    }
  }
  return std::string();
}

void message_transporter::start_xml_rpc_cmd(std::string &xml, const std::string &method_name)
{
  xml = (boost::format("<?xml version=\"1.0\"?><methodCall><methodName>%s</methodName><params>") % method_name).str();
}

void message_transporter::add_xml_rpc_string_param(std::string &xml, const std::string &param)
{
  xml += (boost::format("<param><value><string>%s</string></value></param>") % param).str();
}

void message_transporter::add_xml_rpc_base64_param(std::string &xml, const std::string &param)
{
  // Bitmessage expects some arguments Base64-encoded, but it wants them as parameters of type "string", not "base64" that is also part of XML-RPC
  xml += (boost::format("<param><value><string>%s</string></value></param>") % selamc::to_base64(param)).str();
}

void message_transporter::add_xml_rpc_integer_param(std::string &xml, const int32_t &param)
{
  xml += (boost::format("<param><value><int>%i</int></value></param>") % param).str();
}

void message_transporter::end_xml_rpc_cmd(std::string &xml)
{
  xml += "</params></methodCall>";
}

}
