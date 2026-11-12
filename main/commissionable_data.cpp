#include "commissionable_data.h"

#include <cstring>

#include "esp_log.h"
#include "sdkconfig.h"

#include <crypto/CHIPCryptoPAL.h>
#include <esp_matter_providers.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <platform/CommissionableDataProvider.h>

#include "device_identity.h"

#ifndef CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER
#error "sdkconfig must set CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER=y (see sdkconfig.defaults); delete a stale sdkconfig"
#endif
static_assert(identity::is_valid_passcode(CONFIG_PURIFIER_SETUP_PASSCODE),
              "menuconfig: Air purifier > Pairing passcode is invalid (1-99999998, not a trivial value like 12345678)");
static_assert(identity::is_valid_discriminator(CONFIG_PURIFIER_SETUP_DISCRIMINATOR),
              "menuconfig: Air purifier > Pairing discriminator must be 0-4095");

static const char *TAG = "pairing";

namespace
{

// Same salt and iteration count the CHIP SDK uses for its test setup values.
constexpr uint8_t k_salt[] = { 'S', 'P', 'A', 'K', 'E', '2', 'P', ' ', 'K', 'e', 'y', ' ', 'S', 'a', 'l', 't' };
constexpr uint32_t k_iterations = chip::Crypto::kSpake2p_Min_PBKDF_Iterations; // 1000

class KconfigCommissionableDataProvider : public chip::DeviceLayer::CommissionableDataProvider
{
public:
    CHIP_ERROR GetSetupDiscriminator(uint16_t &discriminator) override
    {
        discriminator = static_cast<uint16_t>(CONFIG_PURIFIER_SETUP_DISCRIMINATOR);
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupDiscriminator(uint16_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

    CHIP_ERROR GetSpake2pIterationCount(uint32_t &count) override
    {
        count = k_iterations;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &buf) override
    {
        if (buf.size() < sizeof(k_salt))
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        memcpy(buf.data(), k_salt, sizeof(k_salt));
        buf.reduce_size(sizeof(k_salt));
        return CHIP_NO_ERROR;
    }

    // Derived on first use (the Matter stack, and with it the crypto layer, is
    // up by then) and cached.
    CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &buf, size_t &out_len) override
    {
        if (m_verifier_len == 0)
        {
            chip::Crypto::Spake2pVerifier verifier;
            CHIP_ERROR err = verifier.Generate(k_iterations, chip::ByteSpan(k_salt), CONFIG_PURIFIER_SETUP_PASSCODE);
            if (err != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "SPAKE2+ verifier generation failed: %" CHIP_ERROR_FORMAT, err.Format());
                return err;
            }
            chip::MutableByteSpan serialized(m_verifier);
            ReturnErrorOnFailure(verifier.Serialize(serialized));
            m_verifier_len = serialized.size();
        }
        out_len = m_verifier_len;
        if (buf.size() < m_verifier_len)
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        memcpy(buf.data(), m_verifier, m_verifier_len);
        buf.reduce_size(m_verifier_len);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetSetupPasscode(uint32_t &passcode) override
    {
        passcode = CONFIG_PURIFIER_SETUP_PASSCODE;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
    chip::Crypto::Spake2pVerifierSerialized m_verifier = {};
    size_t m_verifier_len = 0;
};

KconfigCommissionableDataProvider s_provider;

} // namespace

void commissionable_data_install()
{
    esp_matter::set_custom_commissionable_data_provider(&s_provider);
    ESP_LOGI(TAG, "Pairing discriminator %d (set in menuconfig: Air purifier)", CONFIG_PURIFIER_SETUP_DISCRIMINATOR);
}
