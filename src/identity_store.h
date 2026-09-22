#pragma once
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace zt_identity {
using Validator = std::function<bool(const std::string&)>;
enum class Read { Missing, Ok, Error };
inline Read read(const std::string& path, std::string& value) {
  value.clear();
  int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return errno == ENOENT ? Read::Missing : Read::Error;
  struct stat st{};
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 512) {
    close(fd); return Read::Error;
  }
  char buffer[513];
  for (;;) {
    auto n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { close(fd); return Read::Error; }
    if (!n) break;
    value.append(buffer, static_cast<size_t>(n));
    if (value.size() > 512) { close(fd); return Read::Error; }
  }
  close(fd);
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
  return Read::Ok;
}
inline bool directory(const std::string& path) {
  if (mkdir(path.c_str(), 0700) && errno != EEXIST) return false;
  struct stat st{};
  return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && chmod(path.c_str(), 0700) == 0;
}
inline std::string parent(const std::string& path) {
  return path.substr(0, path.find_last_of('/'));
}
inline bool syncDirectory(const std::string& path) {
  int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  bool ok = fsync(fd) == 0;
  close(fd); return ok;
}
inline bool atomicWrite(const std::string& path, const std::string& value) {
  std::string temp = path + ".XXXXXX";
  int fd = mkstemp(&temp[0]);
  if (fd < 0) return false;
  bool ok = fchmod(fd, 0600) == 0;
  size_t offset = 0;
  while (ok && offset < value.size()) {
    auto n = ::write(fd, value.data() + offset, value.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ok = false; break; }
    offset += static_cast<size_t>(n);
  }
  if (ok) ok = fsync(fd) == 0;
  if (close(fd)) ok = false;
  if (ok) ok = rename(temp.c_str(), path.c_str()) == 0;
  if (!ok) unlink(temp.c_str());
  return ok && syncDirectory(parent(path));
}
// Strict canonical grammar; secret contains address, public key AND private key.
inline bool valid(const std::string& value, bool secret, const Validator& verify) {
  if (value.size() != (secret ? 270u : 141u) || value[10] != ':' ||
      value[11] != '0' || value[12] != ':' || (secret && value[141] != ':')) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (i == 10 || i == 11 || i == 12 || (secret && i == 141)) continue;
    if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return verify(value);
}
inline std::string normalized(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
class Store {
  std::string state, backup;
  Validator verify;
  using Writer = std::function<bool(const std::string&, const std::string&)>;
  Writer write;
  bool fail(const char* reason) { error = reason; return false; }
  bool loadBackup(std::string& secret, bool& exists) {
    auto r = read(backup + "/identity.pair", secret);
    if (r != Read::Missing) {
      exists = true;
      return r == Read::Ok && valid(secret, true, verify);
    }
    std::string pub;
    auto a = read(backup + "/identity.public", pub);
    auto b = read(backup + "/identity.secret", secret);
    exists = a != Read::Missing || b != Read::Missing;
    if (!exists) return true;
    return a == Read::Ok && b == Read::Ok && valid(pub, false, verify) &&
      valid(secret, true, verify) && normalized(pub) == normalized(secret.substr(0, 141));
  }
  bool complete(const std::string& secret) {
    // Persistent transaction remains authoritative across either failed rename.
    if (!write(state + "/identity.secret", secret) ||
        !write(state + "/identity.public", secret.substr(0, 141)))
      return fail("identity_restore_failed");
    std::string a, b;
    if (read(state + "/identity.secret", a) != Read::Ok ||
        read(state + "/identity.public", b) != Read::Ok ||
        a != secret || b != secret.substr(0, 141)) return fail("identity_restore_verify_failed");
    if (unlink((state + "/identity.restore").c_str()) ||
        !syncDirectory(state)) return fail("identity_restore_commit_failed");
    return true;
  }
public:
  std::string error;
  Store(std::string path, Validator validator, Writer writer = atomicWrite)
    : state(std::move(path)), backup(parent(state) + "/identity-backup"),
      verify(std::move(validator)), write(std::move(writer)) {}
  bool prepare() {
    if (!directory(state)) return fail("identity_directory_failed");
    std::string pending, pub, secret;
    const auto transaction = read(state + "/identity.restore", pending);
    if (transaction != Read::Missing) {
      if (transaction != Read::Ok || !valid(pending, true, verify))
        return fail("identity_transaction_invalid");
      return complete(pending);
    }
    auto a = read(state + "/identity.public", pub);
    auto b = read(state + "/identity.secret", secret);
    if (b == Read::Ok && valid(secret, true, verify)) {
      // A valid current private key takes precedence over a stale public file.
      if (a == Read::Ok && normalized(pub) == normalized(secret.substr(0, 141))) return true;
    } else {
      std::string candidate; bool exists = false;
      if (!loadBackup(candidate, exists)) return fail("identity_backup_invalid");
      if (!exists) {
        if (a == Read::Missing && b == Read::Missing) return true; // genuine first boot
        return fail("identity_unrecoverable");
      }
      // Even damaged records may retain a usable node address: never silently
      // switch to a backup of another node. Full valid public keys must match.
      for (const auto& record : {pub, secret}) {
        if (record.size() >= 11 && record[10] == ':' &&
            std::all_of(record.begin(), record.begin()+10,
              [](unsigned char c) { return std::isxdigit(c); }) &&
            normalized(record.substr(0,10)) != normalized(candidate.substr(0,10)))
          return fail("identity_backup_mismatch");
      }
      if (valid(pub, false, verify) &&
          normalized(pub) != normalized(candidate.substr(0,141))) return fail("identity_backup_mismatch");
      secret = candidate;
    }
    if (!write(state + "/identity.restore", secret)) return fail("identity_transaction_write_failed");
    return complete(secret);
  }
  bool saveBackup() {
    std::string pub, secret;
    if (read(state + "/identity.public", pub) != Read::Ok ||
        read(state + "/identity.secret", secret) != Read::Ok ||
        !valid(secret, true, verify) || normalized(pub) != normalized(secret.substr(0,141)))
      return fail("identity_current_invalid");
    std::string previous; bool exists = false;
    const bool previousValid = loadBackup(previous, exists);
    if (previousValid && exists && normalized(previous.substr(0,141)) != normalized(pub))
      return fail("identity_backup_mismatch");
    if (previousValid && exists && normalized(previous) == normalized(secret)) {
      std::string bundled;
      if (read(backup + "/identity.pair", bundled) == Read::Ok) return true;
    }
    if (!directory(backup) || !write(backup + "/identity.pair", secret))
      return fail("identity_backup_failed");
    std::string check;
    if (read(backup + "/identity.pair", check) != Read::Ok || check != secret)
      return fail("identity_backup_verify_failed");
    return true;
  }
};
}
