#include "mpi.h"
#include "mpicxxbase.h"
#include <iostream>

int main( int argc, char *argv[] )
{
  MPI::Init( argc, argv );
  std::cout << "size= " << MPI::COMM_WORLD.Get_size() << "\n";
  std::cout << "myrank = " << MPI::COMM_WORLD.Get_rank() << "\n";
  MPI::Finalize();
  return 0;
}
