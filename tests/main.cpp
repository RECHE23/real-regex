#include <sciforge/test/framework.hpp>

int main(int    argc,
         char** argv)
{
  return test::run_all(argc, argv); // an argument runs only the tests whose name holds it
}
