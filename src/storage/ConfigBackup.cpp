#include "storage/ConfigBackup.h"

#include <FS.h>
#include <LittleFS.h>
#include <cstring>

namespace storage {
namespace {

struct BackupFile {
  const char* name;  // basename in zip
  const char* path;  // LittleFS path
};

constexpr BackupFile kFiles[] = {
    {"pocketops.ini", "/config/pocketops.ini"},
    {"pocketops_bt.ini", "/config/pocketops_bt.ini"},
    {"pocketops_mouse.ini", "/config/pocketops_mouse.ini"},
    {"pocketops_keyboard.ini", "/config/pocketops_keyboard.ini"},
    {"pocketops_folders.ini", "/config/pocketops_folders.ini"},
    {"pocketops_ui.ini", "/config/pocketops_ui.ini"},
    {"pocketops_display.ini", "/config/pocketops_display.ini"},
    {"wifi_profiles.ini", "/config/wifi_profiles.ini"},
};

constexpr size_t kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

void appendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

bool readFileBytes(const char* path, std::vector<uint8_t>& data) {
  data.clear();
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  const size_t n = f.size();
  if (n > ConfigBackup::kMaxZipBytes) {
    f.close();
    return false;
  }
  data.resize(n);
  if (n > 0) {
    const size_t got = f.read(data.data(), n);
    f.close();
    if (got != n) {
      data.clear();
      return false;
    }
  } else {
    f.close();
  }
  return true;
}

const BackupFile* findAllowlisted(const char* name) {
  if (!name || !name[0]) {
    return nullptr;
  }
  // Reject paths / traversal
  if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
    return nullptr;
  }
  for (size_t i = 0; i < kFileCount; ++i) {
    if (strcmp(name, kFiles[i].name) == 0) {
      return &kFiles[i];
    }
  }
  return nullptr;
}

bool writeFileBytes(const char* path, const uint8_t* data, size_t len) {
  File f = LittleFS.open(path, "w");
  if (!f) {
    return false;
  }
  if (len > 0) {
    const size_t w = f.write(data, len);
    f.close();
    return w == len;
  }
  f.close();
  return true;
}

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

void setErr(char* err_out, size_t err_len, const char* msg) {
  if (!err_out || err_len == 0) {
    return;
  }
  strncpy(err_out, msg ? msg : "error", err_len - 1);
  err_out[err_len - 1] = '\0';
}

}  // namespace

bool ConfigBackup::buildZip(std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(4096);

  struct EntryMeta {
    const char* name;
    uint32_t crc;
    uint32_t size;
    uint32_t local_offset;
  };
  EntryMeta metas[kFileCount];
  size_t meta_n = 0;

  for (size_t i = 0; i < kFileCount; ++i) {
    std::vector<uint8_t> body;
    if (!readFileBytes(kFiles[i].path, body)) {
      continue;
    }
    if (out.size() + body.size() + 128 > kMaxZipBytes) {
      out.clear();
      return false;
    }

    EntryMeta& m = metas[meta_n++];
    m.name = kFiles[i].name;
    m.size = static_cast<uint32_t>(body.size());
    m.crc = crc32Update(0, body.data(), body.size());
    m.local_offset = static_cast<uint32_t>(out.size());

    const uint16_t name_len = static_cast<uint16_t>(strlen(m.name));
    // Local file header
    appendU32(out, 0x04034b50u);
    appendU16(out, 20);  // version needed
    appendU16(out, 0);   // flags
    appendU16(out, 0);   // store
    appendU16(out, 0);   // time
    appendU16(out, 0);   // date
    appendU32(out, m.crc);
    appendU32(out, m.size);
    appendU32(out, m.size);
    appendU16(out, name_len);
    appendU16(out, 0);  // extra
    out.insert(out.end(), m.name, m.name + name_len);
    out.insert(out.end(), body.begin(), body.end());
  }

  if (meta_n == 0) {
    out.clear();
    return false;
  }

  const uint32_t cd_offset = static_cast<uint32_t>(out.size());
  for (size_t i = 0; i < meta_n; ++i) {
    const EntryMeta& m = metas[i];
    const uint16_t name_len = static_cast<uint16_t>(strlen(m.name));
    appendU32(out, 0x02014b50u);
    appendU16(out, 20);  // version made by
    appendU16(out, 20);  // version needed
    appendU16(out, 0);
    appendU16(out, 0);  // store
    appendU16(out, 0);
    appendU16(out, 0);
    appendU32(out, m.crc);
    appendU32(out, m.size);
    appendU32(out, m.size);
    appendU16(out, name_len);
    appendU16(out, 0);  // extra
    appendU16(out, 0);  // comment
    appendU16(out, 0);  // disk
    appendU16(out, 0);  // int attrs
    appendU32(out, 0);  // ext attrs
    appendU32(out, m.local_offset);
    out.insert(out.end(), m.name, m.name + name_len);
  }
  const uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_offset;

  // End of central directory
  appendU32(out, 0x06054b50u);
  appendU16(out, 0);
  appendU16(out, 0);
  appendU16(out, static_cast<uint16_t>(meta_n));
  appendU16(out, static_cast<uint16_t>(meta_n));
  appendU32(out, cd_size);
  appendU32(out, cd_offset);
  appendU16(out, 0);  // comment len

  return !out.empty();
}

bool ConfigBackup::applyZip(const uint8_t* data, size_t len, char* err_out, size_t err_len) {
  if (!data || len < 22 || len > kMaxZipBytes) {
    setErr(err_out, err_len, "Invalid or too large zip");
    return false;
  }

  size_t restored = 0;
  size_t pos = 0;
  while (pos + 30 <= len) {
    if (readU32(data + pos) != 0x04034b50u) {
      // Hit central directory or junk — stop scanning locals
      break;
    }
    const uint16_t method = readU16(data + pos + 8);
    const uint32_t crc_exp = readU32(data + pos + 14);
    const uint32_t comp_size = readU32(data + pos + 18);
    const uint32_t uncomp_size = readU32(data + pos + 22);
    const uint16_t name_len = readU16(data + pos + 26);
    const uint16_t extra_len = readU16(data + pos + 28);
    const size_t name_off = pos + 30;
    const size_t data_off = name_off + name_len + extra_len;
    if (name_off + name_len > len || data_off + comp_size > len) {
      setErr(err_out, err_len, "Truncated zip entry");
      return false;
    }
    if (method != 0) {
      setErr(err_out, err_len, "Only uncompressed (store) zip supported");
      return false;
    }
    if (comp_size != uncomp_size) {
      setErr(err_out, err_len, "Corrupt zip sizes");
      return false;
    }

    char name[96];
    if (name_len == 0 || name_len >= sizeof(name)) {
      setErr(err_out, err_len, "Bad entry name");
      return false;
    }
    memcpy(name, data + name_off, name_len);
    name[name_len] = '\0';

    // Allow optional config/ prefix from desktop zips
    const char* base = name;
    if (strncmp(name, "config/", 7) == 0) {
      base = name + 7;
    }

    const BackupFile* slot = findAllowlisted(base);
    if (slot) {
      const uint8_t* payload = data + data_off;
      const uint32_t crc = crc32Update(0, payload, uncomp_size);
      if (crc != crc_exp) {
        setErr(err_out, err_len, "CRC mismatch");
        return false;
      }
      if (!writeFileBytes(slot->path, payload, uncomp_size)) {
        setErr(err_out, err_len, "Write failed");
        return false;
      }
      ++restored;
    }

    pos = data_off + comp_size;
  }

  if (restored == 0) {
    setErr(err_out, err_len, "No known config files in zip");
    return false;
  }

  setErr(err_out, err_len, "OK");
  return true;
}

}  // namespace storage
