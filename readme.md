# WaitForOneFutureOf
Header-only tool to wait for one of multiple `std::future`s

Waits for first of given futures to succeed, then returns its return value. If an exception is rased prior, rethrows.


## Features
- Header-only
- Only exposes namespace relevant to user (`namespace poorly`)
- No dependencies (apart from `stl`)
- **cpp11** & **cpp20** implementations
  - **cpp11**: "traditional" thread + mutex + condition variable implementation
  - **cpp20**: only uses `atomic<bool>`
- Specific optimization for `std::future<void>`
- Automatic cleanup


## Weaknesses
- Oneshot
- Doesn't support non default constructable types
- Uses macro to determine the version of cpp
- Not tested in production

## Examples
### Basic one-liner usage
```cpp
using namespace poorly;

auto f1 = std::async(std::launch::async, func);
auto f2 = std::async(std::launch::async, func);
auto f3 = std::async(std::launch::async, func);

// Pass futures to await on, get new future as result
auto fOneOf3 = waitForOneFutureOf(std::move(f1), std::move(f2), std::move(f3));
try
{
	// Blocks here until f1, f2 or f3 are done/throw exception
    auto p = fOneOf3.get(); 
    std::cout << p << std::endl;
}
catch(std::exception& ex)
{
	// Handle exceptions similarly to singular std::future
    std::cout << ex.what() << std::endl;
}
```

### Dynamic accumulator with manual checking
```cpp
auto f1 = std::async(std::launch::async, func);
auto f2 = std::async(std::launch::async, func);
auto f3 = std::async(std::launch::async, func);

// Make future to await on in afterwards
std::future<int> fut = {};

// Make accumulator to assosiate multiple futures with fut
auto awaiter = make_await_accumulator_for(fut);

// Add as many futures to the accumulator as needed
awaiter.add(std::move(f1));
awaiter.add(std::move(f2));
awaiter.add(std::move(f3));

// Optional check if at least one is done
bool isDone = awaiter.isDone();

// Finally: get() which blocks until f1, f2 or f3 are done/throw exception
auto ret = fut.get();

// do stuff with ret :)
```