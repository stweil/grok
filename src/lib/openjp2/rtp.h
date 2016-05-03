#pragma once

#include <cstdint>
#include <cstddef>

void* sim_payload(uint8_t* buffer, size_t len);
void sim_depayload(void* input, uint8_t** out, size_t* len);

