#pragma once
// A mutual exclusion lock, implemented by the platform (FreeRTOS mutex on the Cardputer). Portable code that may be
// called from two tasks takes one through this interface; NoLock serves single-threaded use and the PC tests.
class Lock {
public:
  virtual ~Lock() {}
  virtual void lock() = 0;
  virtual void unlock() = 0;
};

class NoLock : public Lock {
public:
  void lock() override {}
  void unlock() override {}
};

class LockGuard {
public:
  explicit LockGuard(Lock& l) : _l(l) { _l.lock(); }
  ~LockGuard() { _l.unlock(); }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

private:
  Lock& _l;
};
