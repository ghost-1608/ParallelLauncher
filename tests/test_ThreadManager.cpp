#include <catch2/catch_test_macros.hpp>
#include <ThreadManager.hpp>
#include <set>
#include <thread>
#include <chrono>
#include <unordered_set>

TEST_CASE("ThreadManager: Zero threads construction & destruction", "[unit] [ThreadManager]")
{
  ThreadManager tm;
  REQUIRE_NOTHROW( tm.join() );
  REQUIRE_FALSE  ( tm.all_running() );
  REQUIRE_FALSE  ( tm.any_running() );
  REQUIRE        ( tm.total_threads() == 0 );
  REQUIRE        ( tm.alive_threads() == 0 );
}

TEST_CASE("ThreadManager: Thread construction; thread id, join(), status check validation", "[unit] [ThreadManager]")
{
  ThreadManager tm;

  std::thread::id tid = tm.spawn_thread([](std::stop_token, std::stop_token) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
  });

  REQUIRE        ( tid != std::thread::id{} );

  REQUIRE        ( tm.total_threads() == 1U );
  REQUIRE        ( tm.alive_threads() == 1U );
  REQUIRE        ( tm.all_running() );
  REQUIRE        ( tm.any_running() );

  REQUIRE_NOTHROW( tm.join() );

  REQUIRE        ( tm.total_threads() == 0 );
  REQUIRE        ( tm.alive_threads() == 0 );
  REQUIRE_FALSE  ( tm.any_running() );
  REQUIRE_FALSE  ( tm.all_running() );
}

TEST_CASE("ThreadManager: Reserving space and checking capacity", "[unit] [ThreadManager]")
{
  constexpr unsigned CAPACITY = 10U;
  constexpr unsigned LAUNCH_LIMIT = 2 * CAPACITY;

  ThreadManager tm;
  tm.reserve(CAPACITY);
  REQUIRE( tm.capacity() >= CAPACITY );

  for (int i = 0; i < LAUNCH_LIMIT; i++)
  {
    REQUIRE_NOTHROW( 
      tm.spawn_thread([](std::stop_token, std::stop_token){
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
      }) 
    );
  }

  REQUIRE( tm.capacity() >= LAUNCH_LIMIT );
}

TEST_CASE("ThreadManager: Validating stopping APIs", "[unit] [ThreadManager]")
{
  constexpr unsigned LAUNCH_LIM = 10U;

  ThreadManager tm;
  std::unordered_set<std::thread::id, std::hash<std::thread::id>> tid_set;

  for (int i = 0; i < LAUNCH_LIM; i++)
  {
    tid_set.insert(tm.spawn_thread([](std::stop_token, std::stop_token){ 
      std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }));
  }

  for (const auto& tid : tid_set)
  {
    REQUIRE_FALSE( tm.stop_requested(tid) );
    REQUIRE      ( tm.request_stop(tid) );
    REQUIRE      ( tm.stop_requested(tid) );
  }

  REQUIRE_FALSE( tm.stop_requested_all() );
  REQUIRE      ( tm.request_stop_all() );
  REQUIRE      ( tm.stop_requested_all() );
}

TEST_CASE("ThreadManager: Validating status API functions", "[unit] [ThreadManager]")
{
  constexpr unsigned LAUNCH_LIM = 10U;

  ThreadManager tm;
  std::unordered_set<std::thread::id, std::hash<std::thread::id>> tid_set;

  for (int i = 0; i < LAUNCH_LIM; i++)
  {
    tid_set.insert(tm.spawn_thread([](std::stop_token lst, std::stop_token gst){
      while (!lst.stop_requested() && !gst.stop_requested())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }));
  }

  REQUIRE      ( tm.total_threads() == LAUNCH_LIM );
  REQUIRE      ( tm.alive_threads() == LAUNCH_LIM );
  REQUIRE      ( tm.all_running() );
  REQUIRE      ( tm.any_running() );

  auto it = tid_set.cbegin();

  tm.request_stop(*it);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  REQUIRE      ( tm.total_threads() == LAUNCH_LIM );
  REQUIRE      ( tm.alive_threads() == LAUNCH_LIM - 1U );
  REQUIRE_FALSE( tm.all_running() );
  REQUIRE      ( tm.any_running() );
  
  for (++(++it); it != tid_set.cend(); ++it)
  {
    tm.request_stop(*it);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  REQUIRE      ( tm.alive_threads() == 1U );  
  REQUIRE      ( tm.any_running() );
  
  tm.request_stop_all();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  REQUIRE      ( tm.alive_threads() == 0 );
  REQUIRE_FALSE( tm.any_running() );
}