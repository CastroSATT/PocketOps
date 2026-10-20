#pragma once

#include <Arduino.h>
#include <cstring>

// Compact OUI → vendor (common prefixes only — not full IEEE DB).
namespace lan_scan {

inline const char* ouiVendor(const char* mac) {
  if (!mac || strlen(mac) < 8) {
    return "";
  }
  char p[9];
  size_t j = 0;
  for (size_t i = 0; mac[i] && j < 8; ++i) {
    const char c = mac[i];
    if (c == ':' || c == '-') {
      continue;
    }
    p[j++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  p[j] = '\0';
  if (j < 6) {
    return "";
  }
  // Match on first 6 hex chars (3 octets).
  struct Entry {
    const char* oui;
    const char* name;
  };
  static const Entry kTab[] = {
      {"000C29", "VMware"},      {"00155D", "Microsoft"},  {"001A11", "Google"},
      {"001B63", "Apple"},       {"001C42", "Parallels"},   {"001E06", "Wistron"},
      {"002590", "Super Micro"}, {"0050F2", "Microsoft"},   {"080027", "VirtualBox"},
      {"0C4DE9", "Apple"},       {"101F74", "HewlettPackard"}, {"142233", "Apple"},
      {"18B430", "Nest"},        {"1C1B0D", "Motorola"},    {"204E7F", "Netgear"},
      {"28C538", "Apple"},       {"2C4D54", "ASUS"},        {"30469A", "Netgear"},
      {"3C5A37", "Samsung"},     {"40B0FA", "LG"},          {"44D9E7", "Ubiquiti"},
      {"4C3275", "Apple"},       {"50C7BF", "TP-Link"},     {"525400", "QEMU"},
      {"5CF938", "Apple"},       {"60F189", "Murata"},      {"70B3D5", "IEEE"},
      {"741BB2", "Apple"},       {"783A84", "Apple"},       {"7C2EBD", "Google"},
      {"80E650", "Apple"},       {"84183A", "Roku"},        {"889FFA", "HonHai"},
      {"8C8590", "Apple"},       {"944452", "Samsung"},     {"A4C138", "Telink"},
      {"ACBC32", "Apple"},       {"B827EB", "RaspberryPi"}, {"B8E856", "Apple"},
      {"BC926B", "Apple"},       {"C8D083", "Apple"},       {"CC46D6", "Cisco"},
      {"D0E140", "Apple"},       {"D8A25E", "Apple"},       {"DC56E7", "Apple"},
      {"E0CB4E", "ASUS"},        {"E4F89C", "Intel"},       {"E8EB11", "TexasInstr"},
      {"F0D1A9", "Apple"},       {"F4F5D8", "Google"},      {"F8FFC2", "Apple"},
      {"FC017C", "HonHai"},      {"DC4427", "IEEE"},        {"000000", "Xerox"},
      {"B0A737", "Roku"},        {"18E829", "Ubiquiti"},    {"FCFBFB", "Cisco"},
      {"001E58", "D-Link"},      {"001B11", "D-Link"},      {"C83A35", "Tenda"},
      {"00E04C", "Realtek"},     {"3C37BD", "Amazon"},      {"68FF7B", "TP-Link"},
      {"9C3DCF", "Netgear"},     {"A0BDCD", "Apple"},       {"B0481A", "Apple"},
  };
  for (const auto& e : kTab) {
    if (strncmp(p, e.oui, 6) == 0) {
      return e.name;
    }
  }
  return "";
}

}  // namespace lan_scan
