#include <SignalHandler.hpp>

std::atomic_flag SignalHandler::instance_exists_ = ATOMIC_FLAG_INIT;
std::atomic<uint32_t> SignalHandler::sig_flags_[SIGFLAGN] = {};

SignalHandler::SignalHandler(
  std::initializer_list<int> signal_list,
  int flags,
  bool throw_sig_error
)
{
  if (instance_exists_.test_and_set(std::memory_order::acq_rel))
  {
    throw std::logic_error("Instance of SignalHandler already exists");
  }

  for (const int signal : signal_list)
  {
    if (signal <= 0 || signal >= NSIG)
    {
      instance_exists_.clear(std::memory_order::release);
      throw std::invalid_argument(
        std::to_string(signal) + " is not a valid signal"
      );
    }
  }

  sigemptyset(&sigmask_);
  
  for (const int signal : signal_list)
  {
    sigaddset(&sigmask_, signal);
  }
  
  if (int errc = pthread_sigmask(SIG_BLOCK, &sigmask_, nullptr))   // Only EFAULT possible
  {
    pthread_sigmask(SIG_UNBLOCK, &sigmask_, nullptr);
    instance_exists_.clear(std::memory_order::release);
    throw std::system_error(errc, std::system_category());
  }

  // Reserve space in the map for exactly the number of old handlers required
  old_handlers_.max_load_factor(1.0F);
  old_handlers_.rehash(signal_list.size());

  for (const int signal : signal_list)
  {
    struct sigaction sa_struct, old_sa_struct;
    std::memset(&sa_struct, 0, sizeof(sa_struct));
    sa_struct.sa_flags = flags | SA_SIGINFO;
    sa_struct.sa_sigaction = handler_callback;

    if (
      (sigaction(signal, &sa_struct, &old_sa_struct) == -1) &&
      (throw_sig_error)
    )
    {
      restore_actions();
      instance_exists_.clear(std::memory_order::release);
      throw std::system_error(errno, std::system_category());
    }

    old_handlers_[signal] = old_sa_struct;
  }

  sig_flags_[SIG_FLAGS_ENM::OS_sigs].store(0, std::memory_order::release);
  sig_flags_[SIG_FLAGS_ENM::RT_sigs].store(0, std::memory_order::release);
  sig_flags_[SIG_FLAGS_ENM::reserved].store(0, std::memory_order::release);

  dispatch_thread_ = std::jthread(
    dispatcher,
    std::ref(dispatch_mtx_),
    std::ref(dispatch_cv_),
    &sigmask_
  );
}

SignalHandler::SignalHandler(
  std::initializer_list<std::pair<int, int>> initialiser_list,
  bool throw_sig_error
)
{
  if (instance_exists_.test_and_set(std::memory_order::acq_rel))
  {
    throw std::logic_error("Instance of SignalHandler already exists");
  }

  for (const auto [signal, _] : initialiser_list)
  {
    if (signal <= 0 || signal >= NSIG)
    {
      instance_exists_.clear(std::memory_order::release);
      throw std::invalid_argument(
        std::to_string(signal) + " is not a valid signal"
      );
    }
  }

  sigemptyset(&sigmask_);
  
  for (const auto [signal, _] : initialiser_list)
  {
    sigaddset(&sigmask_, signal);
  }
  
  if (int errc = pthread_sigmask(SIG_BLOCK, &sigmask_, nullptr))   // Only EFAULT possible
  {
    pthread_sigmask(SIG_UNBLOCK, &sigmask_, nullptr);
    instance_exists_.clear(std::memory_order::release);
    throw std::system_error(errc, std::system_category());
  }

  // Reserve space in the map for exactly the number of old handlers required
  old_handlers_.max_load_factor(1.0F);
  old_handlers_.rehash(initialiser_list.size());

  for (const auto [signal, flags] : initialiser_list)
  {
    struct sigaction sa_struct, old_sa_struct;
    std::memset(&sa_struct, 0, sizeof(sa_struct));
    sa_struct.sa_flags = flags | SA_SIGINFO;
    sa_struct.sa_sigaction = handler_callback;

    if (
      (sigaction(signal, &sa_struct, &old_sa_struct) == -1) &&
      (throw_sig_error)
    )
    {
      restore_actions();
      instance_exists_.clear(std::memory_order::release);
      throw std::system_error(errno, std::system_category());
    }

    old_handlers_[signal] = old_sa_struct;
  }

  sig_flags_[SIG_FLAGS_ENM::OS_sigs].store(0, std::memory_order::release);
  sig_flags_[SIG_FLAGS_ENM::RT_sigs].store(0, std::memory_order::release);
  sig_flags_[SIG_FLAGS_ENM::reserved].store(0, std::memory_order::release);

  dispatch_thread_ = std::jthread(
    dispatcher,
    std::ref(dispatch_mtx_),
    std::ref(dispatch_cv_),
    &sigmask_
  );
}

void SignalHandler::handler_callback(int sig, siginfo_t* info, void* ucontext) noexcept
{
  if (sig < 32) 
  {
    sig_flags_[SIG_FLAGS_ENM::OS_sigs].fetch_or(
      signal_bitmask(sig),
      std::memory_order::release
    );
    return;
  }
  if (sig < 64)
  {  
    sig_flags_[SIG_FLAGS_ENM::RT_sigs].fetch_or(
      signal_bitmask(sig),
      std::memory_order::release
    );
    return;
  }
  // Path reserved for future expansion
  [[unlikely]] sig_flags_[SIG_FLAGS_ENM::reserved].fetch_or(
    signal_bitmask(sig),
    std::memory_order::release
  );
}

SignalHandler::~SignalHandler()
{ 
  restore_actions();

  {
    std::lock_guard lock(dispatch_mtx_);
    dispatch_thread_.request_stop();
  }
  dispatch_cv_.notify_one();

  // Destructor must not throw
  try
  {
    dispatch_thread_.join();
  }
  catch (...)
  { }

  sig_flags_[SIG_FLAGS_ENM::OS_sigs].store(0, std::memory_order::release);
  sig_flags_[SIG_FLAGS_ENM::RT_sigs].store(0, std::memory_order::release);
  sig_flags_[SIG_FLAGS_ENM::reserved].store(0, std::memory_order::release);

  instance_exists_.clear(std::memory_order::release);
}