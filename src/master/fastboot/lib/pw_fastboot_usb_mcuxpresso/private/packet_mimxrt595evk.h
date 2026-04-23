#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>

#include "pw_fastboot/transport.h"

extern "C" {

/* Inform that a fastboot packet was received from the host.
 *
 * This must be called from the USB sample.
 */
void OnFastbootPacketReceived(uint8_t const* buf, size_t len);

/* Inform that a previously queued packet was sent to the host.
 *
 * This must be called from the USB sample.
 */
void OnFastbootPacketSent(uint8_t const* buf, size_t len);
}

#include "pw_bytes/span.h"

namespace fastboot::mimxrt595evk {

/* Initialize the USB fastboot transport
 *
 * This must be called before sending any packets using
 * FastbootSendPacket and FastbootReceivePacket.
 */
void UsbTransportInit();

/* Enqueue a fastboot packet to send back to the host.
 *
 * The caller must be within a FreeRTOS task!
 */
std::ptrdiff_t FastbootSendPacket(pw::ConstByteSpan);

/* Read a fastboot packet from the host into the specified ByteSpan.
 *
 * The caller must be within a FreeRTOS task!
 */
std::ptrdiff_t FastbootReceivePacket(pw::ByteSpan);

}  // namespace fastboot::mimxrt595evk
