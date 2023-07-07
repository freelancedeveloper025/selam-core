#pragma once

#include <selamc/bt_producer.h>

// Compatibility shim for selamc includes

namespace selammq {

using selamc::bt_list_producer;
using selamc::bt_dict_producer;

} // namespace selammq
