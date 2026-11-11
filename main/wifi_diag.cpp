#include "wifi_diag.h"

namespace wifi_diag
{

const char *reason_name(uint8_t reason)
{
    switch (reason)
    {
    case 1: return "UNSPECIFIED";
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "DISASSOC_DUE_TO_INACTIVITY";
    case 5: return "ASSOC_TOOMANY";
    case 6: return "CLASS2_FRAME_FROM_NONAUTH_STA";
    case 7: return "CLASS3_FRAME_FROM_NONASSOC_STA";
    case 8: return "ASSOC_LEAVE";
    case 9: return "ASSOC_NOT_AUTHED";
    case 10: return "DISASSOC_PWRCAP_BAD";
    case 11: return "DISASSOC_SUPCHAN_BAD";
    case 12: return "BSS_TRANSITION_DISASSOC";
    case 13: return "IE_INVALID";
    case 14: return "MIC_FAILURE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
    case 17: return "IE_IN_4WAY_DIFFERS";
    case 18: return "GROUP_CIPHER_INVALID";
    case 19: return "PAIRWISE_CIPHER_INVALID";
    case 20: return "AKMP_INVALID";
    case 21: return "UNSUPP_RSN_IE_VERSION";
    case 22: return "INVALID_RSN_IE_CAP";
    case 23: return "802_1X_AUTH_FAILED";
    case 24: return "CIPHER_SUITE_REJECTED";
    case 25: return "TDLS_PEER_UNREACHABLE";
    case 26: return "TDLS_UNSPECIFIED";
    case 27: return "SSP_REQUESTED_DISASSOC";
    case 28: return "NO_SSP_ROAMING_AGREEMENT";
    case 29: return "BAD_CIPHER_OR_AKM";
    case 30: return "NOT_AUTHORIZED_THIS_LOCATION";
    case 31: return "SERVICE_CHANGE_PERCLUDES_TS";
    case 32: return "UNSPECIFIED_QOS";
    case 33: return "NOT_ENOUGH_BANDWIDTH";
    case 34: return "MISSING_ACKS";
    case 35: return "EXCEEDED_TXOP";
    case 36: return "STA_LEAVING";
    case 37: return "END_BA";
    case 38: return "UNKNOWN_BA";
    case 39: return "TIMEOUT";
    case 46: return "PEER_INITIATED";
    case 47: return "AP_INITIATED";
    case 48: return "INVALID_FT_ACTION_FRAME_COUNT";
    case 49: return "INVALID_PMKID";
    case 50: return "INVALID_MDE";
    case 51: return "INVALID_FTE";
    case 67: return "TRANSMISSION_LINK_ESTABLISH_FAILED";
    case 68: return "ALTERATIVE_CHANNEL_OCCUPIED";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    case 206: return "AP_TSF_RESET";
    case 207: return "ROAMING";
    case 208: return "ASSOC_COMEBACK_TIME_TOO_LONG";
    case 209: return "SA_QUERY_TIMEOUT";
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    default: return "UNKNOWN";
    }
}

const char *reason_hint(uint8_t reason)
{
    switch (reason)
    {
    case 2:   // AUTH_EXPIRE
    case 15:  // 4WAY_HANDSHAKE_TIMEOUT
    case 204: // HANDSHAKE_TIMEOUT
        return "usually a wrong Wi-Fi password: check the credentials the controller sent";
    case 14: // MIC_FAILURE
        return "wrong password or a corrupted key exchange";
    case 13: // IE_INVALID
    case 17: // IE_IN_4WAY_DIFFERS
    case 20: // AKMP_INVALID
    case 22: // INVALID_RSN_IE_CAP
    case 29: // BAD_CIPHER_OR_AKM
        return "security settings mismatch: try WPA2-Personal (AES) only, PMF optional, 802.11r off on the 2.4 GHz network";
    case 18: // GROUP_CIPHER_INVALID
    case 19: // PAIRWISE_CIPHER_INVALID
    case 24: // CIPHER_SUITE_REJECTED
        return "cipher not accepted: use AES (CCMP), not TKIP";
    case 49: // INVALID_PMKID
    case 50: // INVALID_MDE
    case 51: // INVALID_FTE
    case 48: // INVALID_FT_ACTION_FRAME_COUNT
        return "fast roaming (802.11r) problem: turn 802.11r off for this network";
    case 201: // NO_AP_FOUND
        return "network not found: check the SSID and that it is on 2.4 GHz (the ESP32 has no 5 GHz)";
    case 210: // NO_AP_FOUND_W_COMPATIBLE_SECURITY
    case 211: // NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
        return "the network's security isn't supported: this firmware needs WPA2 (WPA3-only networks can't be joined)";
    case 212: // NO_AP_FOUND_IN_RSSI_THRESHOLD
    case 200: // BEACON_TIMEOUT
        return "weak signal: move the purifier or the access point closer";
    case 5: // ASSOC_TOOMANY
        return "the access point has too many clients";
    case 202: // AUTH_FAIL
    case 203: // ASSOC_FAIL
        return "the access point refused the connection: check MAC filtering and client limits";
    default:
        return nullptr;
    }
}

const char *authmode_name(int authmode)
{
    switch (authmode)
    {
    case 0: return "OPEN";
    case 1: return "WEP";
    case 2: return "WPA_PSK";
    case 3: return "WPA2_PSK";
    case 4: return "WPA_WPA2_PSK";
    case 5: return "WPA2_ENTERPRISE";
    case 6: return "WPA3_PSK";
    case 7: return "WPA2_WPA3_PSK";
    case 8: return "WAPI_PSK";
    case 9: return "OWE";
    case 10: return "WPA3_ENT_192";
    case 13: return "DPP";
    case 14: return "WPA3_ENTERPRISE";
    case 15: return "WPA2_WPA3_ENTERPRISE";
    case 16: return "WPA_ENTERPRISE";
    default: return "UNKNOWN";
    }
}

DisconnectFilter::Decision DisconnectFilter::on_disconnect(uint8_t reason)
{
    Decision d;
    if (m_count > 0 && reason != m_reason)
    {
        d.previous_reason = m_reason;
        d.previous_count = m_count;
        m_count = 0;
    }
    m_reason = reason;
    m_count++;
    d.repeats = m_count;
    d.log = m_count == 1 || (m_every > 0 && m_count % m_every == 0);
    return d;
}

uint32_t DisconnectFilter::on_connected(uint8_t &reason)
{
    uint32_t count = m_count;
    reason = m_reason;
    m_count = 0;
    return count;
}

} // namespace wifi_diag
