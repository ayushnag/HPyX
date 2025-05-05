#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>
#include <hpx/hpx_start.hpp>
#include <hpx/iostream.hpp>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace nb = nanobind;
using namespace nb::literals;

int hpx_hello()
{
    // Say hello to the world!
    hpx::cout << "Hello World!\n" << std::flush;
    return 0;
}

void hpx_async_add(int a, int b)
{
    auto add = [](int number, int value_to_add)
    {
        return number + value_to_add;
    };

    hpx::future<int> add_lazy = hpx::async(add, a, b);
    std::cout << "Calling hpx::async(add, " << a << ", " << b
              << ")" << std::endl;
    int lazy_result = add_lazy.get();
    std::cout << "Which returned: " << lazy_result << std::endl;
}

struct global_runtime_manager
{
    global_runtime_manager(std::vector<std::string> const& config)
      : running_(false), rts_(nullptr), cfg(config)
    {
        hpx::init_params params;
        params.cfg = cfg;
        params.mode = hpx::runtime_mode::console;

        hpx::function<int(int, char**)> start_function =
            hpx::bind_front(&global_runtime_manager::hpx_main, this);

        if (!hpx::start(start_function, 0, nullptr, params))
        {
            std::abort(); // Failed to initialize runtime
        }

        // Wait for the main HPX thread to start
        std::unique_lock<std::mutex> lk(startup_mtx_);
        while (!running_)
            startup_cond_.wait(lk);
    }

    ~global_runtime_manager()
    {
        {
            std::lock_guard<hpx::spinlock> lk(mtx_);
            rts_ = nullptr;
        }

        cond_.notify_one();
        hpx::stop(); // Stop the runtime
    }

    int hpx_main(int argc, char* argv[])
    {
        rts_ = hpx::get_runtime_ptr();

        {
            std::lock_guard<std::mutex> lk(startup_mtx_);
            running_ = true;
        }

        startup_cond_.notify_one();

        // Wait for the destructor to signal exit
        {
            std::unique_lock<hpx::spinlock> lk(mtx_);
            if (rts_ != nullptr)
                cond_.wait(lk);
        }

        return hpx::finalize(); // Allow runtime to exit
    }

private:
    hpx::spinlock mtx_;
    hpx::condition_variable_any cond_;

    std::mutex startup_mtx_;
    std::condition_variable startup_cond_;
    bool running_;

    hpx::runtime* rts_;
    std::vector<std::string> const cfg;
};

global_runtime_manager* rts = nullptr;

void init_hpx_runtime(std::vector<std::string> const& cfg)
{
    if (rts == nullptr)
    {
        nb::gil_scoped_acquire acquire;
        rts = new global_runtime_manager(cfg);
    }
}

void stop_hpx_runtime()
{
    global_runtime_manager* r = rts;
    rts = nullptr;
    if (r != nullptr)
    {
        nb::gil_scoped_release release;
        delete r;
    }
}

NB_MODULE(_core, m) {
    m.doc() = "Python bindings for HPX C++ API";
    m.def("add", [](int a, int b) { return a + b; }, "a"_a, "b"_a);
    m.def("hpx_hello", &hpx_hello);
    m.def("hpx_async_add", &hpx_async_add, "a"_a, "b"_a);

    m.def("hpx_async", [](const nb::callable& fn) {
        // Create HPX future
        auto hpx_fut = hpx::async([fn]() {
            nb::gil_scoped_acquire acquire;
            return fn();
        });
        
        // Always try to get the running loop (we assume we're called from an async context)
        auto asyncio = nb::module_::import_("asyncio");
        nb::object loop = asyncio.attr("get_running_loop")();
        
        // Create a Python Future on that loop
        auto py_fut = loop.attr("create_future")();
        
        // Keep shared copies to ensure they live long enough
        auto shared_loop = std::make_shared<nb::object>(loop);
        auto shared_py_fut = std::make_shared<nb::object>(py_fut);
        
        // Attach a continuation to set the result
        hpx_fut.then([shared_py_fut, shared_loop](hpx::future<nb::object> f) {
            nb::object result;
            std::exception_ptr eptr;
            
            try {
                nb::gil_scoped_release release;
                result = f.get();
            } catch (...) {
                eptr = std::current_exception();
            }
            
            nb::gil_scoped_acquire acquire;
            if (!eptr) {
                // Schedule setting the result on the asyncio event loop thread.
                (*shared_loop).attr("call_soon_threadsafe")(
                    nb::cpp_function([shared_py_fut, result]() {
                        (*shared_py_fut).attr("set_result")(result);
                    })
                );
            }
        });
        
        return py_fut;
    });
    m.def("init_hpx_runtime", &init_hpx_runtime);
    m.def("stop_hpx_runtime", &stop_hpx_runtime);

    #ifdef VERSION_INFO
        m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
    #else
        m.attr("__version__") = "dev";
    #endif
}
