// A simple program to test and show how poorly::waitForOneFutureOf is utilized
// to wait for one of multiple futures (or throws from them), ignoring everything that happends after.

// Note: will not fork for future<void>
// Note: uses atomic for cpp20+, otherwise uses mutex+cvar 

#include "waitForOneFutureOf.h"


#include <iostream>
#include <chrono>


int func(int n) 
{
    static int i = 2;
    std::cout << n << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(n * 100));
    if(!(i--))
    {
        i = 2;
        throw std::runtime_error("Fooooooo!");
    }
    return n;
}
#include <vector>
int main()
{
    using namespace poorly;

    //std::vector<std::future<int>> vect, v2 = {};
    //vect.push_back(std::async(std::launch::async, func, 2));
    
    /* Check futures with void
    {
        auto f1 = std::async(std::launch::async, []()
        {
            int asdf = 2;
        });

        auto fOneOf1 = waitForOneFutureOf(std::move(f1));
        
        fOneOf1.get();
        bool breakPoint = true;
        while(breakPoint)
        {}
    }
    //*/

    /* Check simple one-liner usage, probably used most often
    {
        auto f1 = std::async(std::launch::async, func, 2);
        auto f2 = std::async(std::launch::async, func, 3);
        auto f3 = std::async(std::launch::async, func, 1);
        
        auto fOneOf3 = waitForOneFutureOf(std::move(f1), std::move(f2), std::move(f3));
        try
        {
            auto p = fOneOf3.get();
            

            bool breakPoint = true;
            while(breakPoint)
            { }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << std::endl;
        }
    }
    //*/
    
    
    /* Check Accumulator and ::isDone for cases where manual ready check is needed
    {
        auto f1 = std::async(std::launch::async, func, 2);
        auto f2 = std::async(std::launch::async, func, 3);
        auto f3 = std::async(std::launch::async, func, 1);

        
        std::future<int> fut = {};
        auto awaiter = make_await_accumulator_for(fut);


        awaiter.add(std::move(f1));
        awaiter.add(std::move(f2));
        awaiter.add(std::move(f3));
        bool isDone = awaiter.isDone();
        auto ret = fut.get();


        bool breakPoint = false;
        while(breakPoint)
        { }
    }
    //*/
    

}
