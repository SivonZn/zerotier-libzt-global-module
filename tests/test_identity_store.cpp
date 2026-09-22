#include "identity_store.h"
#include <cassert>
#include <iostream>
using namespace zt_identity;
const std::string pub = "123456789a:0:" + std::string(128, 'a');
const std::string secret = pub + ":" + std::string(128, 'b');
const std::string otherPub = "223456789a:0:" + std::string(128, 'c');
const std::string other = otherPub + ":" + std::string(128, 'd');
bool verify(const std::string& s) { return s == pub || s == secret || s == otherPub || s == other; }
std::string get(const std::string& p) { std::string s; assert(read(p,s) == Read::Ok); return s; }
std::string testdir(const std::string& root) {
  std::string p = root + "/case.XXXXXX";
  assert(mkdtemp(&p[0])); assert(directory(p + "/state")); return p;
}
void seed(const std::string& p) {
  assert(atomicWrite(p+"/state/identity.public",pub));
  assert(atomicWrite(p+"/state/identity.secret",secret));
  Store s(p+"/state",verify); assert(s.saveBackup());
}
int main(int argc, char** argv) {
  assert(argc == 2);
  for (const std::string field : {"public", "secret"}) {
    for (const std::string& damage : {std::string("missing"), std::string(""), std::string("truncated"), pub}) {
      auto p = testdir(argv[1]); seed(p);
      auto path = p+"/state/identity."+field;
      if (damage == "missing") assert(unlink(path.c_str()) == 0);
      else assert(atomicWrite(path,damage));
      Store s(p+"/state",verify); assert(s.prepare());
      assert(get(p+"/state/identity.secret") == secret);
      assert(get(p+"/state/identity.public") == pub);
    }
  }
  for (int failAt : {1,2,3}) {
    auto p = testdir(argv[1]); seed(p);
    assert(unlink((p+"/state/identity.secret").c_str()) == 0);
    int writes = 0;
    Store s(p+"/state",verify,[&](const std::string& path,const std::string& value) {
      return ++writes != failAt && atomicWrite(path,value);
    });
    assert(!s.prepare()); assert(get(p+"/identity-backup/identity.pair") == secret);
    Store retry(p+"/state",verify); assert(retry.prepare());
    assert(get(p+"/state/identity.secret") == secret);
    assert(get(p+"/state/identity.public") == pub);
  }
  {
    auto p=testdir(argv[1]); seed(p);
    assert(atomicWrite(p+"/state/identity.public",otherPub));
    assert(atomicWrite(p+"/state/identity.secret","broken"));
    Store s(p+"/state",verify); assert(!s.prepare());
    assert(s.error == "identity_backup_mismatch");
    assert(get(p+"/state/identity.secret") == "broken");
  }
  {
    auto p=testdir(argv[1]); seed(p);
    assert(atomicWrite(p+"/state/identity.public",otherPub));
    assert(atomicWrite(p+"/state/identity.secret",other));
    Store s(p+"/state",verify); assert(!s.saveBackup());
    assert(get(p+"/identity-backup/identity.pair") == secret);
  }
  {
    auto p=testdir(argv[1]); seed(p);
    assert(atomicWrite(p+"/state/identity.secret",pub));
    Store s(p+"/state",verify); assert(!s.saveBackup());
    assert(get(p+"/identity-backup/identity.pair") == secret);
  }
  {
    auto p=testdir(argv[1]);
    Store s(p+"/state",verify); assert(s.prepare());
    assert(atomicWrite(p+"/state/identity.public",pub));
    assert(!s.prepare());
  }
  {
    auto p=testdir(argv[1]); assert(directory(p+"/identity-backup"));
    assert(atomicWrite(p+"/identity-backup/identity.public",pub));
    assert(atomicWrite(p+"/identity-backup/identity.secret",secret));
    Store s(p+"/state",verify); assert(s.prepare());
    Store failed(p+"/state",verify,[](const std::string&, const std::string&){return false;});
    assert(!failed.saveBackup());
    assert(get(p+"/identity-backup/identity.secret") == secret);
    assert(s.saveBackup()); assert(get(p+"/identity-backup/identity.pair") == secret);
  }
  {
    auto p=testdir(argv[1]); seed(p);
    assert(atomicWrite(p+"/state/identity.restore","bad"));
    Store s(p+"/state",verify); assert(!s.prepare());
    assert(s.error == "identity_transaction_invalid");
  }
  for (int failAt : {1,2,3}) {
    auto p = testdir(argv[1]); seed(p);
    assert(unlink((p+"/state/identity.public").c_str()) == 0);
    int writes = 0;
    Store interrupted(p+"/state",verify,[&](const std::string& path,const std::string& value) {
      bool ok = atomicWrite(path,value);
      return ok && ++writes != failAt;
    });
    assert(!interrupted.prepare());
    Store retry(p+"/state",verify); assert(retry.prepare());
    assert(get(p+"/state/identity.secret") == secret);
    assert(get(p+"/state/identity.public") == pub);
  }
  {
    auto p=testdir(argv[1]); seed(p);
    assert(unlink((p+"/state/identity.public").c_str()) == 0);
    assert(mkfifo((p+"/state/identity.public").c_str(),0600) == 0);
    Store s(p+"/state",verify); assert(s.prepare());
    assert(get(p+"/state/identity.public") == pub);
    struct stat st{};
    assert(stat((p+"/identity-backup/identity.pair").c_str(),&st) == 0);
    assert((st.st_mode & 0777) == 0600);
  }
  std::cout << "identity recovery/transaction/backup regressions passed\n";
}
