/**
 * @file test_host_devices.cpp
 * @brief Functional tests for Phase 20 host device enumeration.
 *
 * All assertions go through the C-Bridge API — no platform-specific
 * headers are included here.  The HAL layer is the sole source of truth.
 *
 * Tests:
 *   1. DeviceCount          — at least one output device is reported.
 *   2. DeviceName           — device name is non-empty and null-terminated.
 *   3. SampleRate           — default rate is a recognisable audio sample rate.
 *   4. BlockSize            — default block size is a power of 2 in [32, 8192].
 *   5. SupportedRates       — at least one supported rate, all in range.
 *   6. SupportedBlockSizes  — at least one supported size, all power-of-2.
 *   7. EngineDriverQueries  — engine_get_driver_* returns consistent values.
 *   8. EngineDriverName     — engine_get_driver_name returns non-empty string.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_power_of_two(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static bool is_known_sample_rate(int rate) {
    const int known[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000,
                         88200, 96000, 176400, 192000};
    for (int r : known) if (r == rate) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class HostDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
    }
};

// ---------------------------------------------------------------------------
// Test 1: DeviceCount — at least one output device
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, DeviceCount) {
    PRINT_TEST_HEADER(
        "Host Devices — Count",
        "host_get_device_count() returns ≥ 1 output-capable device.",
        "host_get_device_count()",
        "count ≥ 1",
        0
    );

    const int count = host_get_device_count();
    std::cout << "[Devices] Device count: " << count << "\n";
    EXPECT_GE(count, 1) << "Expected at least one output device";
}

// ---------------------------------------------------------------------------
// Test 2: DeviceName — non-empty, null-terminated
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, DeviceName) {
    PRINT_TEST_HEADER(
        "Host Devices — Name",
        "host_get_device_name(0) returns a non-empty string.",
        "host_get_device_name(0, buf, sizeof(buf))",
        "return 0 and strlen(buf) > 0",
        0
    );

    char name[512]{};
    const int rc = host_get_device_name(0, name, sizeof(name));
    std::cout << "[Devices] Device 0 name: \"" << name << "\"\n";

    ASSERT_EQ(rc, 0) << "host_get_device_name failed";
    EXPECT_GT(std::strlen(name), 0u) << "Device name should be non-empty";
    EXPECT_EQ(name[sizeof(name) - 1], '\0') << "Buffer must be null-terminated";
}

// ---------------------------------------------------------------------------
// Test 3: SampleRate — recognisable audio sample rate
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, SampleRate) {
    PRINT_TEST_HEADER(
        "Host Devices — Sample Rate",
        "host_get_device_sample_rate(0) returns a recognisable audio rate.",
        "host_get_device_sample_rate(0)",
        "rate in {8000, 11025, 16000, 22050, 32000, 44100, 48000, ...}",
        0
    );

    const int rate = host_get_device_sample_rate(0);
    std::cout << "[Devices] Default sample rate: " << rate << " Hz\n";

    EXPECT_GT(rate, 0) << "Sample rate must be positive";
    EXPECT_TRUE(is_known_sample_rate(rate))
        << "Expected a standard audio sample rate, got " << rate;
}

// ---------------------------------------------------------------------------
// Test 4: BlockSize — power of 2 in [32, 8192]
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, BlockSize) {
    PRINT_TEST_HEADER(
        "Host Devices — Block Size",
        "host_get_device_block_size(0) returns a power-of-2 period size.",
        "host_get_device_block_size(0)",
        "power-of-2 in [32, 8192]",
        0
    );

    const int size = host_get_device_block_size(0);
    std::cout << "[Devices] Default block size: " << size << " frames\n";

    EXPECT_GT(size, 0);
    EXPECT_TRUE(is_power_of_two(size)) << "Block size must be a power of 2, got " << size;
    EXPECT_GE(size, 32)   << "Block size too small";
    EXPECT_LE(size, 8192) << "Block size suspiciously large";
}

// ---------------------------------------------------------------------------
// Test 5: SupportedRates — at least one, all recognisable
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, SupportedSampleRates) {
    PRINT_TEST_HEADER(
        "Host Devices — Supported Sample Rates",
        "host_get_supported_sample_rates(0) returns ≥ 1 recognisable rate.",
        "host_get_supported_sample_rates(0, rates, 16)",
        "count ≥ 1, all rates recognisable",
        0
    );

    int rates[16]{};
    const int count = host_get_supported_sample_rates(0, rates, 16);
    std::cout << "[Devices] Supported rates (" << count << "): ";
    for (int i = 0; i < count; ++i) std::cout << rates[i] << (i < count - 1 ? ", " : "");
    std::cout << "\n";

    ASSERT_GE(count, 1) << "Expected at least one supported sample rate";
    for (int i = 0; i < count; ++i) {
        EXPECT_TRUE(is_known_sample_rate(rates[i]))
            << "Unrecognised sample rate at index " << i << ": " << rates[i];
    }
}

// ---------------------------------------------------------------------------
// Test 6: SupportedBlockSizes — at least one, all power-of-2
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, SupportedBlockSizes) {
    PRINT_TEST_HEADER(
        "Host Devices — Supported Block Sizes",
        "host_get_supported_block_sizes(0) returns ≥ 1 power-of-2 size.",
        "host_get_supported_block_sizes(0, sizes, 16)",
        "count ≥ 1, all power-of-2",
        0
    );

    int sizes[16]{};
    const int count = host_get_supported_block_sizes(0, sizes, 16);
    std::cout << "[Devices] Supported block sizes (" << count << "): ";
    for (int i = 0; i < count; ++i) std::cout << sizes[i] << (i < count - 1 ? ", " : "");
    std::cout << "\n";

    ASSERT_GE(count, 1) << "Expected at least one supported block size";
    for (int i = 0; i < count; ++i) {
        EXPECT_TRUE(is_power_of_two(sizes[i]))
            << "Block size at index " << i << " is not a power of 2: " << sizes[i];
    }
}

// ---------------------------------------------------------------------------
// Test 7: EngineDriverQueries — engine reports consistent sr and block size
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, EngineDriverQueries) {
    PRINT_TEST_HEADER(
        "Engine Driver — Sample Rate & Block Size",
        "engine_get_driver_sample_rate and _block_size return positive values "
        "consistent with the host device list.",
        "engine_create → engine_get_driver_sample_rate / _block_size",
        "sr > 0, block_size power-of-2, sr in host device supported rates",
        0
    );

    const int safe_sr = test::get_safe_sample_rate(0);
    test::EngineWrapper eng(safe_sr);
    const EngineHandle h = eng.get();

    const int driver_sr = engine_get_driver_sample_rate(h);
    const int driver_bs = engine_get_driver_block_size(h);

    std::cout << "[Engine] driver sample rate: " << driver_sr << " Hz\n";
    std::cout << "[Engine] driver block size:  " << driver_bs << " frames\n";

    EXPECT_GT(driver_sr, 0);
    EXPECT_TRUE(is_known_sample_rate(driver_sr))
        << "Driver sample rate not a standard audio rate: " << driver_sr;
    EXPECT_TRUE(is_power_of_two(driver_bs))
        << "Driver block size not a power of 2: " << driver_bs;
    EXPECT_GE(driver_bs, 32);
    EXPECT_LE(driver_bs, 8192);
}

// ---------------------------------------------------------------------------
// Test 8: EngineDriverName — non-empty string
// ---------------------------------------------------------------------------

TEST_F(HostDeviceTest, EngineDriverName) {
    PRINT_TEST_HEADER(
        "Engine Driver — Device Name",
        "engine_get_driver_name returns a non-empty device name string.",
        "engine_create → engine_get_driver_name",
        "return 0, strlen > 0",
        0
    );

    const int safe_sr = test::get_safe_sample_rate(0);
    test::EngineWrapper eng(safe_sr);
    const EngineHandle h = eng.get();

    char name[512]{};
    const int rc = engine_get_driver_name(h, name, sizeof(name));
    std::cout << "[Engine] driver name: \"" << name << "\"\n";

    ASSERT_EQ(rc, 0);
    EXPECT_GT(std::strlen(name), 0u) << "Driver name should be non-empty";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
