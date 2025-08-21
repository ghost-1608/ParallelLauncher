/**
 *  ===========================================================================
 * /                             SignalHandler                                /
 * ===========================================================================
 *             -- A class to simplify handling Linux signals --
 * 
 * > SignalHandler provides the API to abstract handling Linux signals
 * 
 * > Utilities aside from the class:-
 *   (+) uint32_t sigbitmask(int) - Provides a bitmask for the given signal
 *   (+) struct SignalMask_t - Used to return information via the API functions
 * 
 * > The class has the following public methods:-
 *   (+) Constructor (<list of signals>[, <flags>][, <error switch>])
 *   (+) Constructor (<list of <signals, flags>>[, <error switch>])
 * 
 *   (+) bool test_signal(int sig) - Tests if the given signal was raised
 *   (+) bool pop_signal(int sig) - Same as above and clears signal state
 *   (+) bool test_all_signals(SignalMask_t& sigbitmask) - Tests all signals
 *   (+) bool pop_all_signals(SignalMask_t& sigbitmask) - Same as above and
 *                                                        clears signal states
 * 
 * > SignalHandler is MT-safe and converts an asynchronous signal experience to 
 *   synchronous.
 * > Only a single instance of SignalHandler can be instantiated at once, in a 
 *   process.
 * > SignalHandler is designed to handle signals for the whole process
 * > Ideally, an instance should be created before any threads are created.
 *   SignalHandler internally relies on pthread_sigmask and sigaction to 
 *   ensure signals are delivered predictably to an internal thread. By the 
 *   nature of pthread_sigmask, threads created after calling it will inherit 
 *   the mask and hence will not disrupt the performance of SignalHandler. 
 *   However, threads created prior, will obviously continue to receive signals 
 *   unblocked, unless manually blocked. This would create a race condition and 
 *   would result in an undefined behaviour from SignalHandler.
 */

#pragma once


#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>


/** 
 * For the given signal, returns a uint32 bitmask shifted according to the
 * category of the signal: OS or RT signals; 0 (zero) if signo is illegal. 
 * (refer POSIX signals)
 * 
 * @param signo Signal to convert from
 * @returns The uint32 bitmask of the signal 
 */ 
inline constexpr uint32_t signal_bitmask(int signo) noexcept
{
  if (signo <= 0 || signo >= NSIG)
  {
    return 0;
  }
  if (signo < 32)
  {
    return 1U << (signo - 1);
  }
  if (signo < 64)
  {
    return 1U << (signo - 32);
  }
  // Path reserved for future expansion
  [[unlikely]] return 1U << (signo - 64);
}

///  @brief Bitmask used as a return type by test all signals-family of 
///         class methods of SignalHandler
struct SignalMask_t
{
  // Stores bitmask of signals belonging to the OS category
  uint32_t OS_sigs;
  // Stores bitmask of signals belonging to the RT category
  uint32_t RT_sigs;
  // Reserved for future expansion
  uint32_t reserved;
};

/// @brief Allows registered signals to be handled and queried synchronously 
///        per process
class SignalHandler
{
  // Magic number: Size of signal bitmask array
  static constexpr size_t SIGFLAGN = 3UL;
  // Enum to index signal bitmask array
  enum SIG_FLAGS_ENM { OS_sigs, RT_sigs, reserved };

public:
  SignalHandler(const SignalHandler&) = delete;
  SignalHandler& operator= (const SignalHandler&) = delete;
  SignalHandler(SignalHandler&&) = delete;
  SignalHandler& operator= (SignalHandler&&) = delete;

  SignalHandler() = delete;

  /**
   * Constructs an instance of SignalHandler with the given set of signals and
   * optional flags
   * @param signal_list Set of signals to register
   * @param flags Flags (refer to sigaction)
   * @param throw_sig_error Throws errors (if any) while setting up handler for
   *                        every signal when true, else skips faulty installs
   */
  SignalHandler(
    std::initializer_list<int> signal_list, 
    int flags = 0,
    bool throw_sig_error = true
  );

  /**
   * Constructs an instance of SignalHandler with the given set of signals and
   * their respective flags
   * 
   * @param initialiser_list Pairs of signals to register and their respective 
   *                         flags (refer sigaction)
   * @param throw_sig_error Throws errors (if any) while setting up handler for
   *                        every signal when true, else skips faulty installs
   */
  SignalHandler(
    std::initializer_list<std::pair<int, int>> initialiser_list,
    bool throw_sig_error = true
  );
  ~SignalHandler();

  /**
   * Tests whether a given signal was raised
   * 
   * @param sig Signal to test for
   * @returns True if signal was raised, false if signal was not raised
   *          (or) signal state was cleared
   */
  bool test_signal(int sig) const noexcept
  {
    if (sig <= 0 || sig >= NSIG)
    {
      return false;
    }

    SIG_FLAGS_ENM flagenm = SIG_FLAGS_ENM::OS_sigs;
    if (sig >= 32 && sig < 64)
    {
      flagenm = SIG_FLAGS_ENM::RT_sigs;
    }
    // Path reserved for future expansion
    else if (sig >= 64) [[unlikely]]
    {
      flagenm = SIG_FLAGS_ENM::reserved;
    }

    return sig_flags_[flagenm].load(std::memory_order::acquire) &
           signal_bitmask(sig);
  }

  /**
   * Tests if any signals were raised and provides the signal states
   * as a bitmask
   * 
   * @param sigbitmask Modifies the given SignalMask_t instance to 
   *                   store the bitmask of the signal states
   * @returns True if a signal was raised, false if no signals were
   *          raised (or) signal states were cleared
   */
  bool test_all_signals(SignalMask_t& sigbitmask) noexcept
  {
    sigbitmask.OS_sigs = sig_flags_[SIG_FLAGS_ENM::OS_sigs]
                                .load(std::memory_order::acquire);
    sigbitmask.RT_sigs = sig_flags_[SIG_FLAGS_ENM::RT_sigs]
                                .load(std::memory_order::acquire);
    // Reserved for future expansion
    sigbitmask.reserved = sig_flags_[SIG_FLAGS_ENM::reserved]
                                .load(std::memory_order::acquire);

    return sigbitmask.OS_sigs ||
           sigbitmask.RT_sigs ||
           sigbitmask.reserved;
  }

  /**
   * Tests whether a given signal was raised and clears its state
   * 
   * @param sig Signal to test for
   * @returns True if signal was raised, false if signal was not raised
   *          (or) signal state was cleared
   */
  bool pop_signal(int sig) noexcept
  {
    if (sig <= 0 || sig >= NSIG)
    {
      return false;
    }

    SIG_FLAGS_ENM flagenm = SIG_FLAGS_ENM::OS_sigs;
    if (sig >= 32 && sig < 64)
    {
      flagenm = SIG_FLAGS_ENM::RT_sigs;
    }
    // Path reserved for future expansion
    else if (sig >= 64) [[unlikely]]
    {
      flagenm = SIG_FLAGS_ENM::reserved;
    }

    return sig_flags_[flagenm].fetch_and(
      ~ signal_bitmask(sig), 
      std::memory_order::release
    );
  }

  /**
   * Tests if any signals were raised and provides the signal states
   * as a bitmask, clears all signal states
   * 
   * @param sigbitmask Modifies the given SignalMask_t instance to 
   *                   store the bitmask of the signal states
   * @returns True if a signal was raised, false if no signals were
   *          raised (or) signal states were cleared
   */
  bool pop_all_signals(SignalMask_t& sigbitmask) noexcept
  {
    bool ret = test_all_signals(sigbitmask);
    sig_flags_[SIG_FLAGS_ENM::OS_sigs].store(0, std::memory_order::release);
    sig_flags_[SIG_FLAGS_ENM::RT_sigs].store(0, std::memory_order::release);
    // Reserved for future expansion
    sig_flags_[SIG_FLAGS_ENM::reserved].store(0, std::memory_order::release);
    return ret;
  }


private:
  // Callback to be installed as an action using sigaction
  static void handler_callback(int, siginfo_t*, void*) noexcept;

  // Lambda used to create the dispatcher thread
  static inline auto dispatcher = [] (
    const std::stop_token& stoken,
    std::mutex& dispatch_mtx_,
    std::condition_variable& cv,
    const sigset_t* sigmask_ptr
  ) noexcept 
  {
    /**
     * Is designed to keep the thread waiting to be interrupted by sigaction
     * This eliminates the "hijacking" behaviour of sigaction
     */

    pthread_sigmask(SIG_UNBLOCK, sigmask_ptr, nullptr);

    std::unique_lock lock(dispatch_mtx_);
    cv.wait(lock, [stoken] { return stoken.stop_requested(); });
  };

  // Restores all old actions of each signal and unblocks the signals
  void restore_actions() noexcept
  {
    for (const auto& it : old_handlers_)
    {
      int signum = it.first;
      const struct sigaction* handler_ptr = &it.second;
      sigaction(signum, handler_ptr, nullptr);
    }
    pthread_sigmask(SIG_UNBLOCK, &sigmask_, nullptr);
  }

  // Enforces singleton behaviour
  static std::atomic_flag instance_exists_;

  // Array to store the signal state bitmasks
  static std::atomic<uint32_t> sig_flags_[SIGFLAGN];

  // Stores mask of the registered signals
  sigset_t sigmask_;
  // Stores the existing handlers
  std::unordered_map<int, struct sigaction, std::hash<int>> old_handlers_;

  // Dispatch thread
  std::jthread dispatch_thread_;
  // Condition variable the dispatch thread waits on
  std::condition_variable dispatch_cv_;
  // Mutex for the dispatch thread condition variable
  std::mutex dispatch_mtx_;
};