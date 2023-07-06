#include "common/string_util.h"
#include "cryptonote_config.h"
#include "cryptonote_basic/hardfork.h"
#include "common/selam.h"
#include "epee/int-util.h"
#include <selamc/endian.h>
#include <limits>
#include <vector>
#include <boost/lexical_cast.hpp>
#include <cfenv>

#include "selam_economy.h"
#include "service_node_rules.h"

using cryptonote::hf;

namespace service_nodes {

uint64_t get_staking_requirement(cryptonote::network_type nettype, hf hardfork)
{
  assert(hardfork >= hf::hf16_pulse);
  return nettype == cryptonote::network_type::MAINNET
    ? selam::STAKING_REQUIREMENT
    : selam::STAKING_REQUIREMENT_TESTNET;
}

// TODO(selam): Move to selam_economy, this will also need access to selam::exp2
uint64_t get_staking_requirement(cryptonote::network_type nettype, uint64_t height)
{
  if (nettype != cryptonote::network_type::MAINNET)
      return selam::STAKING_REQUIREMENT_TESTNET;

  if (is_hard_fork_at_least(nettype, hf::hf16_pulse, height))
    return selam::STAKING_REQUIREMENT;

  if (is_hard_fork_at_least(nettype, hf::hf13_enforce_checkpoints, height))
  {
    constexpr int64_t heights[] = {
        385824,
        429024,
        472224,
        515424,
        558624,
        601824,
        645024,
    };

    constexpr int64_t lsr[] = {
        20458'380815527,
        19332'319724305,
        18438'564443912,
        17729'190407764,
        17166'159862153,
        16719'282221956,
        16364'595203882,
    };

    assert(static_cast<int64_t>(height) >= heights[0]);
    constexpr uint64_t LAST_HEIGHT      = heights[selam::array_count(heights) - 1];
    constexpr uint64_t LAST_REQUIREMENT = lsr    [selam::array_count(lsr) - 1];
    if (height >= LAST_HEIGHT)
        return LAST_REQUIREMENT;

    size_t i = 0;
    for (size_t index = 1; index < selam::array_count(heights); index++)
    {
      if (heights[index] > static_cast<int64_t>(height))
      {
        i = (index - 1);
        break;
      }
    }

    int64_t H      = height;
    int64_t result = lsr[i] + (H - heights[i]) * ((lsr[i + 1] - lsr[i]) / (heights[i + 1] - heights[i]));
    return static_cast<uint64_t>(result);
  }

  uint64_t hardfork_height = 101250;
  if (height < hardfork_height) height = hardfork_height;

  uint64_t height_adjusted = height - hardfork_height;
  uint64_t base = 0, variable = 0;
  std::fesetround(FE_TONEAREST);
  if (is_hard_fork_at_least(nettype, hf::hf11_infinite_staking, height))
  {
    base     = 15000 * selam::COIN;
    variable = (25007.0 * selam::COIN) / selam::exp2(height_adjusted/129600.0);
  }
  else
  {
    base      = 10000 * selam::COIN;
    variable  = (35000.0 * selam::COIN) / selam::exp2(height_adjusted/129600.0);
  }

  uint64_t result = base + variable;
  return result;
}

uint64_t portions_to_amount(uint64_t portions, uint64_t staking_requirement)
{
  return mul128_div64(staking_requirement, portions, cryptonote::old::STAKING_PORTIONS);
}

bool check_service_node_portions(hf hf_version, const std::vector<std::pair<cryptonote::account_public_address, uint64_t>>& portions)
{
  // When checking portion we always use HF18 rules, even on HF19, because a registration actually
  // generated under HF19+ won't get here.
  if (hf_version == hf::hf19_reward_batching)
    hf_version = hf::hf18;
  else if (hf_version > hf::hf19_reward_batching)
  {
    LOG_PRINT_L1("Registration tx rejected: portions-based registrations not permitted after HF19");
    return false;
  }
  if (portions.size() > selam::MAX_CONTRIBUTORS_V1) {
    LOG_PRINT_L1("Registration tx rejected: too many contributors (" << portions.size() << " > " << selam::MAX_CONTRIBUTORS_V1 << ")");
    return false;
  }

  uint64_t reserved = 0;
  uint64_t remaining = cryptonote::old::STAKING_PORTIONS;
  for (size_t i = 0; i < portions.size(); ++i)
  {

    const uint64_t min_portions = get_min_node_contribution(hf_version, cryptonote::old::STAKING_PORTIONS, reserved, i);
    if (portions[i].second < min_portions) {
      LOG_PRINT_L1("Registration tx rejected: portion " << i << " too small (" << portions[i].second << " < " << min_portions << ")");
      return false;
    }
    if (portions[i].second > remaining) {
      LOG_PRINT_L1("Registration tx rejected: portion " << i << " exceeds available portions");
      return false;
    }

    reserved += portions[i].second;
    remaining -= portions[i].second;
  }

  return true;
}

bool check_service_node_stakes(hf hf_version, cryptonote::network_type nettype, uint64_t staking_requirement, const std::vector<std::pair<cryptonote::account_public_address, uint64_t>>& stakes)
{
  if (hf_version < hf::hf19_reward_batching) {
    LOG_PRINT_L1("Registration tx rejected: amount-based registrations not accepted before HF19");
    return false; // SELAM-based registrations not accepted before HF19
  }
  if (stakes.size() > selam::MAX_CONTRIBUTORS_HF19) {
    LOG_PRINT_L1("Registration tx rejected: too many contributors (" << stakes.size() << " > " << selam::MAX_CONTRIBUTORS_HF19 << ")");
    return false;
  }

  const auto operator_requirement = nettype == cryptonote::network_type::MAINNET
    ? selam::MINIMUM_OPERATOR_CONTRIBUTION
    : selam::MINIMUM_OPERATOR_CONTRIBUTION_TESTNET;

  uint64_t reserved = 0;
  uint64_t remaining = staking_requirement;
  for (size_t i = 0; i < stakes.size(); i++) {
    const uint64_t min_stake = i == 0 ? operator_requirement : get_min_node_contribution(hf_version, staking_requirement, reserved, i);

    if (stakes[i].second < min_stake) {
      LOG_PRINT_L1("Registration tx rejected: stake " << i << " too small (" << stakes[i].second << " < " << min_stake << ")");
      return false;
    }
    if (stakes[i].second > remaining) {
      LOG_PRINT_L1("Registration tx rejected: stake " << i << " (" << stakes[i].second << ") exceeds available remaining stake (" << remaining << ")");
      return false;
    }

    reserved += stakes[i].second;
    remaining -= stakes[i].second;
  }

  return true;
}

crypto::hash generate_request_stake_unlock_hash(uint32_t nonce)
{
  static_assert(sizeof(crypto::hash) == 8 * sizeof(uint32_t) && alignof(crypto::hash) >= alignof(uint32_t));
  crypto::hash result;
  selamc::host_to_little_inplace(nonce);
  for (size_t i = 0; i < 8; i++)
    reinterpret_cast<uint32_t*>(result.data)[i] = nonce;
  return result;
}

uint64_t get_locked_key_image_unlock_height(cryptonote::network_type nettype, uint64_t node_register_height, uint64_t curr_height)
{
  uint64_t blocks_to_lock = staking_num_lock_blocks(nettype);
  uint64_t result         = curr_height + (blocks_to_lock / 2);
  return result;
}

static uint64_t get_min_node_contribution_pre_v11(uint64_t staking_requirement, uint64_t total_reserved)
{
  return std::min(staking_requirement - total_reserved, staking_requirement / selam::MAX_CONTRIBUTORS_V1);
}

uint64_t get_max_node_contribution(hf version, uint64_t staking_requirement, uint64_t total_reserved)
{
  if (version >= hf::hf16_pulse)
    return (staking_requirement - total_reserved) * cryptonote::MAXIMUM_ACCEPTABLE_STAKE::num
      / cryptonote::MAXIMUM_ACCEPTABLE_STAKE::den;
  return std::numeric_limits<uint64_t>::max();
}

uint64_t get_min_node_contribution(hf version, uint64_t staking_requirement, uint64_t total_reserved, size_t num_contributions)
{
  if (version < hf::hf11_infinite_staking)
    return get_min_node_contribution_pre_v11(staking_requirement, total_reserved);

  const uint64_t needed = staking_requirement - total_reserved;

  const size_t max_contributors = version >= hf::hf19_reward_batching ? selam::MAX_CONTRIBUTORS_HF19 : selam::MAX_CONTRIBUTORS_V1;
  if (max_contributors <= num_contributions) return UINT64_MAX;

  const size_t num_contributions_remaining_avail = max_contributors - num_contributions;
  return needed / num_contributions_remaining_avail;
}

uint64_t get_min_node_contribution_in_portions(hf version, uint64_t staking_requirement, uint64_t total_reserved, size_t num_contributions)
{
  uint64_t atomic_amount = get_min_node_contribution(version, staking_requirement, total_reserved, num_contributions);
  uint64_t result        = (atomic_amount == UINT64_MAX) ? UINT64_MAX : (get_portions_to_make_amount(staking_requirement, atomic_amount));
  return result;
}

uint64_t get_portions_to_make_amount(uint64_t staking_requirement, uint64_t amount, uint64_t max_portions)
{
  uint64_t lo, hi, resulthi, resultlo;
  lo = mul128(amount, max_portions, &hi);
  if (lo > UINT64_MAX - (staking_requirement - 1))
    hi++;
  lo += staking_requirement-1;
  div128_64(hi, lo, staking_requirement, &resulthi, &resultlo);
  return resultlo;
}

static bool get_portions_from_percent(double cur_percent, uint64_t& portions) {
  if(cur_percent < 0.0 || cur_percent > 100.0) return false;

  // Fix for truncation issue when operator cut = 100 for a pool Service Node.
  if (cur_percent == 100.0)
  {
    portions = cryptonote::old::STAKING_PORTIONS;
  }
  else
  {
    portions = (cur_percent / 100.0) * (double)cryptonote::old::STAKING_PORTIONS;
  }

  return true;
}

std::optional<double> parse_fee_percent(std::string_view fee)
{
  if (tools::ends_with(fee, "%"))
    fee.remove_suffix(1);

  double percent;
  try {
    percent = boost::lexical_cast<double>(fee);
  } catch(...) {
    return std::nullopt;
  }

  if (percent < 0 || percent > 100)
    return std::nullopt;

  return percent;
}

bool get_portions_from_percent_str(std::string cut_str, uint64_t& portions) {

  if (auto pct = parse_fee_percent(cut_str))
    return get_portions_from_percent(*pct, portions);

  return false;
}

} // namespace service_nodes
