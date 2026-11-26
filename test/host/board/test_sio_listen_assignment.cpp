// rb_board: SioListenAssignment and validate_for_sio_listen() (H5, design of
// rb_esp_sio, delivery E1). The listen assignment carries COMMAND and RX only:
// it has no TX to hand out, and only validate_for_sio_listen() creates one.
//
// Every profile used here has its H4 gate NOT signed unless a test makes a
// test-only signed copy; nothing here approves H4, H4-L or H4-T.

#include <cstdint>
#include <type_traits>

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::check_sio_pins;
using rb::board::fujinet_atari_v1x_profile;
using rb::board::kNoPin;
namespace PinWarning = rb::board::PinWarning;
using rb::board::SdInterface;
using rb::board::SdSpiPins;
using rb::board::SioAssignment;
using rb::board::SioListenAssignment;
using rb::board::SioMode;
using rb::board::SioPins;
using rb::board::SpiHost;
using rb::board::unconfirmed_profile;
using rb::board::validate_for_sio;
using rb::board::validate_for_sio_listen;
using rb::core::ErrorCode;

// --- No TX by construction ---------------------------------------------------------
// Neither a tx member (data or function) nor the full pin set, which holds TX.
template <typename T, typename = void>
struct HasTx : std::false_type {};
template <typename T>
struct HasTx<T, std::void_t<decltype(&T::tx)>> : std::true_type {};

template <typename T, typename = void>
struct HasPins : std::false_type {};
template <typename T>
struct HasPins<T, std::void_t<decltype(&T::pins)>> : std::true_type {};

static_assert(!HasTx<SioListenAssignment>::value, "the listen assignment must not expose TX");
static_assert(!HasPins<SioListenAssignment>::value, "the listen assignment must not carry SioPins (it holds TX)");
static_assert(HasTx<SioPins>::value && HasPins<SioAssignment>::value, "the detectors must see TX where it exists");

// Only validate_for_sio_listen() creates one: no default, no pins, no conversion.
static_assert(!std::is_default_constructible<SioListenAssignment>::value, "no unauthorized default value");
static_assert(!std::is_constructible<SioListenAssignment, int8_t, int8_t, uint8_t>::value,
              "drivers must not build their own assignment");
static_assert(!std::is_constructible<SioListenAssignment, SioAssignment>::value, "no conversion from SioAssignment");
static_assert(!std::is_constructible<SioListenAssignment, SioPins>::value, "no conversion from SioPins");
static_assert(std::is_copy_constructible<SioListenAssignment>::value, "copyable, so it can travel in a Result");

namespace {

// microSD on 18/23/19/4 (no strapping pin) plus the given SIO pins; H4 gate not signed.
BoardProfile sio_board(SioPins sio)
{
    return BoardProfile{"test-fixture", true, false, SdInterface::Spi, SdSpiPins{18, 23, 19, 4}, SpiHost::Spi3,
                        kNoPin, false, sio, kNoPin, false, false};
}

// A test-only copy with the gate signed: the real profiles stay unapproved.
BoardProfile signed_copy(BoardProfile profile)
{
    profile.sioGateApproved = true;
    return profile;
}

ErrorCode refusal(const BoardProfile& profile)
{
    const auto result = validate_for_sio_listen(profile);
    TEST_ASSERT_FALSE_MESSAGE(result.ok(), "expected listening to be refused");
    return result.error();
}

// validate_for_sio_listen() must agree with validate_for_sio(ReceiveOnly) on
// every outcome, minus the TX line.
void assert_same_as_receive_only(const BoardProfile& profile)
{
    const auto listen = validate_for_sio_listen(profile);
    const auto receive_only = validate_for_sio(profile, SioMode::ReceiveOnly);
    TEST_ASSERT_EQUAL(receive_only.ok(), listen.ok());
    if (!listen.ok()) {
        TEST_ASSERT_TRUE(listen.error() == receive_only.error());
        return;
    }
    TEST_ASSERT_EQUAL_INT8(receive_only.value().pins.command, listen.value().command());
    TEST_ASSERT_EQUAL_INT8(receive_only.value().pins.rx, listen.value().rx());
    TEST_ASSERT_EQUAL_UINT8(receive_only.value().warnings, listen.value().warnings());
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- The gate stays closed ---------------------------------------------------------

static void test_default_profile_refuses_listening(void)
{
    TEST_ASSERT_TRUE(refusal(unconfirmed_profile()) == ErrorCode::BoardNotConfirmed);
}

static void test_fujinet_profile_refuses_listening_while_h4_is_not_signed(void)
{
    const BoardProfile profile = fujinet_atari_v1x_profile();
    TEST_ASSERT_FALSE(profile.sioGateApproved);
    TEST_ASSERT_TRUE(refusal(profile) == ErrorCode::BoardNotConfirmed);
}

static void test_valid_pins_without_the_gate_are_refused(void)
{
    const BoardProfile candidate = sio_board(SioPins{39, 33, 21});
    TEST_ASSERT_TRUE(check_sio_pins(candidate, SioMode::ReceiveOnly).ok());  // the pins are fine...
    TEST_ASSERT_TRUE(refusal(candidate) == ErrorCode::BoardNotConfirmed);    // ...but not signed
    TEST_ASSERT_FALSE(candidate.sioGateApproved);                            // asking approved nothing
}

static void test_unconfirmed_hardware_is_refused_even_with_the_gate_signed(void)
{
    BoardProfile profile = signed_copy(sio_board(SioPins{39, 33, kNoPin}));
    profile.hardwareConfirmed = false;
    TEST_ASSERT_TRUE(refusal(profile) == ErrorCode::BoardNotConfirmed);
}

static void test_transmit_authorization_is_unchanged(void)
{
    const BoardProfile profile = fujinet_atari_v1x_profile();
    const auto receive_only = validate_for_sio(profile, SioMode::ReceiveOnly);
    const auto receive_transmit = validate_for_sio(profile, SioMode::ReceiveTransmit);
    TEST_ASSERT_FALSE(receive_only.ok());
    TEST_ASSERT_TRUE(receive_only.error() == ErrorCode::BoardNotConfirmed);
    TEST_ASSERT_FALSE(receive_transmit.ok());
    TEST_ASSERT_TRUE(receive_transmit.error() == ErrorCode::BoardNotConfirmed);
}

// --- What a signed gate would grant (test-only copies) -------------------------------

static void test_signed_fujinet_copy_grants_command_and_rx_only(void)
{
    const auto result = validate_for_sio_listen(signed_copy(fujinet_atari_v1x_profile()));
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(39, result.value().command());  // COMMAND
    TEST_ASSERT_EQUAL_INT8(33, result.value().rx());       // Atari DATA OUT; GPIO21 is not reachable at all
}

static void test_signed_gate_still_needs_valid_pins(void)
{
    TEST_ASSERT_TRUE(refusal(signed_copy(sio_board(SioPins{39, 33, 35}))) ==
                     ErrorCode::InvalidPinAssignment);  // a wired TX on an input-only GPIO is still checked
    TEST_ASSERT_TRUE(refusal(signed_copy(sio_board(SioPins{39, kNoPin, kNoPin}))) ==
                     ErrorCode::InvalidPinAssignment);  // no RX
    TEST_ASSERT_TRUE(refusal(signed_copy(sio_board(SioPins{39, 19, kNoPin}))) ==
                     ErrorCode::InvalidPinAssignment);  // RX on the microSD MISO
}

static void test_pin_warnings_are_kept(void)
{
    const auto result = validate_for_sio_listen(signed_copy(sio_board(SioPins{39, 12, kNoPin})));  // RX on MTDI
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_TRUE(result.value().warnings() & PinWarning::kStrappingPin);
    TEST_ASSERT_TRUE(result.value().warnings() & PinWarning::kFlashVoltageStrap);
}

static void test_agrees_with_receive_only_authorization(void)
{
    assert_same_as_receive_only(unconfirmed_profile());
    assert_same_as_receive_only(fujinet_atari_v1x_profile());
    assert_same_as_receive_only(signed_copy(fujinet_atari_v1x_profile()));
    assert_same_as_receive_only(sio_board(SioPins{39, 33, 21}));
    assert_same_as_receive_only(signed_copy(sio_board(SioPins{39, 33, 21})));
    assert_same_as_receive_only(signed_copy(sio_board(SioPins{39, 36, kNoPin})));
    assert_same_as_receive_only(signed_copy(sio_board(SioPins{39, 12, kNoPin})));
    assert_same_as_receive_only(signed_copy(sio_board(SioPins{39, 33, 35})));
    assert_same_as_receive_only(signed_copy(sio_board(SioPins{39, 39, kNoPin})));
}

static void test_a_copy_keeps_the_granted_pins(void)
{
    const auto result = validate_for_sio_listen(signed_copy(fujinet_atari_v1x_profile()));
    TEST_ASSERT_TRUE(result.ok());
    const SioListenAssignment copy = result.value();
    TEST_ASSERT_EQUAL_INT8(39, copy.command());
    TEST_ASSERT_EQUAL_INT8(33, copy.rx());
    TEST_ASSERT_EQUAL_UINT8(result.value().warnings(), copy.warnings());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_profile_refuses_listening);
    RUN_TEST(test_fujinet_profile_refuses_listening_while_h4_is_not_signed);
    RUN_TEST(test_valid_pins_without_the_gate_are_refused);
    RUN_TEST(test_unconfirmed_hardware_is_refused_even_with_the_gate_signed);
    RUN_TEST(test_transmit_authorization_is_unchanged);
    RUN_TEST(test_signed_fujinet_copy_grants_command_and_rx_only);
    RUN_TEST(test_signed_gate_still_needs_valid_pins);
    RUN_TEST(test_pin_warnings_are_kept);
    RUN_TEST(test_agrees_with_receive_only_authorization);
    RUN_TEST(test_a_copy_keeps_the_granted_pins);
    return UNITY_END();
}
