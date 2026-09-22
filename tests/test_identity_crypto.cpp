#include <ZeroTierSockets.h>
#include "identity_store.h"
#include <cassert>
#include <iostream>
bool verify(const std::string& value) {
  char key[ZTS_ID_STR_BUF_LEN]{};
  if (value.size() >= sizeof(key)) return false;
  memcpy(key, value.data(), value.size());
  return zts_id_pair_is_valid(key, sizeof(key)) == 1;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  char key[ZTS_ID_STR_BUF_LEN]{};
  unsigned int len = sizeof(key);
  assert(zts_id_new(key, &len) == ZTS_ERR_OK);
  std::string secret(key,len), pub=secret.substr(0,141);
  assert(zt_identity::valid(secret,true,verify));
  assert(zt_identity::valid(pub,false,verify));
  for (size_t index : {160u, 230u}) { // DH and signing halves
    auto corrupt = secret;
    corrupt[index] = corrupt[index] == 'a' ? 'b' : 'a';
    assert(!zt_identity::valid(corrupt,true,verify));
  }
  assert(!zt_identity::valid(pub,true,verify));
  assert(!zt_identity::valid(secret+"garbage",true,verify));
  std::string state = std::string(argv[1]) + "/state";
  assert(zt_identity::directory(state));
  assert(zt_identity::atomicWrite(state+"/identity.secret",secret));
  zt_identity::Store store(state,verify);
  assert(store.prepare()); assert(store.saveBackup());
  assert(zt_identity::atomicWrite(state+"/identity.secret","broken"));
  assert(store.prepare());
  std::string restored;
  assert(zt_identity::read(state+"/identity.secret",restored) == zt_identity::Read::Ok);
  assert(restored == secret);
  std::cout << "real ZeroTier key validation and identity-preserving recovery passed\n";
}
