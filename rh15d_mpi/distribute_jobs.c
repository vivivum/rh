/* ------- file: -------------------------- distribute_jobs.c ------------

       Version:       rh2.0, 1.5-D plane-parallel
       Author:        Tiago Pereira (tiago.pereira@nasa.gov)
       Last modified: Wed Dec 29 20:58:00 2010 --

       --------------------------                      ----------RH-- */

/* --- Divides all the 1D columns between the different MPI processes --- */

#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "rh.h"
#include "atom.h"
#include "atmos.h"
#include "geometry.h"
#include "inputs.h"
#include "parallel.h"
#include "error.h"
#include "io.h"


/* --- Function prototypes --                          -------------- */

int   *intrange(int start, int end, int step, int *N);
long  *get_tasks(long ntotal, int size);
long **get_taskmap(long *ntasks, long *my_start);
long **matrix_long(long Nrow, long Ncol);

/* --- Global variables --                             -------------- */

extern MPI_data mpi;
extern InputData input;
extern char messageStr[];
extern NCDF_Atmos_file infile;


/* ------- begin --------------------------   distribute_jobs.c   --- */
void distribute_jobs(void)
{
  /* The end product of this routine must be:

     For each process:
         * mpi.Ntasks --DONE
	 * An array of rank (mpi.Ntasks,2) that gives (mpi.ix, mpi.iy) 
           for each task index --DONE (mpi.my_tasks)
	 * A map of (mpi.ix, mpi.iy) -> (xi, yi) --DONE (two 1D arrays: mpi.xnum, mpi.ynum)

 */

  long *tasks;
 
  mpi.backgrrecno = 0; 

  /* Sanitise input */
  if ((input.p15d_x0 < 0)) input.p15d_x0 = 0;
  if ((input.p15d_y0 < 0)) input.p15d_y0 = 0;

  if ((input.p15d_x0 > input.p15d_x1)) input.p15d_x0 = input.p15d_x1;
  if ((input.p15d_y0 > input.p15d_y1)) input.p15d_y0 = input.p15d_y1;

  if ((input.p15d_x1 <= 0) || (input.p15d_x1 > infile.nx))
    input.p15d_x1 = infile.nx;
  if ((input.p15d_y1 <= 0) || (input.p15d_y1 > infile.ny))
    input.p15d_y1 = infile.ny;

  if ((input.p15d_xst < 1)) input.p15d_xst = 1;
  if ((input.p15d_yst < 1)) input.p15d_yst = 1;

  /* Calculate array maps of (mpi.ix/iy) > xi/yi */
  mpi.xnum = intrange(input.p15d_x0, input.p15d_x1, input.p15d_xst, &mpi.nx);
  mpi.ynum = intrange(input.p15d_y0, input.p15d_y1, input.p15d_yst, &mpi.ny);

  /* Abort if more processes than tasks (avoid idle processes waiting forever */
  if (mpi.size > (long) mpi.nx*mpi.ny) {
    sprintf(messageStr,
            "\n*** More MPI processes (%d) than tasks (%ld), aborting.\n",
	    mpi.size, (long) mpi.nx*mpi.ny);
    Error(ERROR_LEVEL_2, "distribute_jobs", messageStr);
  }

  /* Calculate tasks and distribute */ 
  tasks        = get_tasks((long) mpi.nx*mpi.ny, mpi.size);
  mpi.Ntasks   = tasks[mpi.rank];
  mpi.taskmap  = get_taskmap(tasks, &mpi.my_start);

  free(tasks);

  return;
}
/* ------- end   --------------------------   distribute_jobs.c   --- */

/* ------- begin --------------------------   get_tasks.c ------- --- */
long *get_tasks(long ntotal, int size)
/* Divides the ntotal tasks by 'size' processes */
{
  long i, *tasks;

  tasks = (long *) malloc(size * sizeof(long));
  
  for (i=0; i < size; i++) tasks[i] = ntotal/size;

  /* Distribute remaining */
  if ((ntotal % size) > 0) {
    for (i=0; i < ntotal % size; i++) ++tasks[i];
  }
  
  return tasks;
}
/* ------- end   --------------------------   get_tasks.c ------- --- */

/* ------- begin --------------------------   get_taskmap.c ----- --- */
long **get_taskmap(long *ntasks, long *my_start)
{
  long i, j, k, *start, **taskmap;

  taskmap = matrix_long((long) mpi.nx*mpi.ny, (long) 2);
  start   = (long *) malloc(mpi.size * sizeof(long));

  /* Create map of tasks */
  k = 0;
  for (i=0; i < mpi.nx; i++) {
    for (j=0; j < mpi.ny; j++) {
      taskmap[k][0] = i;
      taskmap[k][1] = j;
      ++k;
    }
  }

  /* distribute tasks */
  k = 0;
  for (i=0; i < mpi.size; i++) { 
    start[i] = k;
    k += ntasks[i];
  }

  /* pointer to each process's starting task */
  *my_start = start[mpi.rank];

  free(start);

  return taskmap;
}
/* ------- end   --------------------------   get_taskmap.c ----- --- */

/* ------- begin --------------------------   intrange.c -------- --- */
int *intrange(int start, int end, int step, int *N)
/* Mimics Python's range function. Also gives a pointer
   to the number of elements. */
{
  int i, *arange;

  *N = (end - start) / step;
  if  ((end - start) % step > 0) ++*N;

  arange = (int *) malloc(*N * sizeof(int));

  arange[0] = start;
  for (i=1; i < *N; i++) {
    arange[i] = arange[i-1] + step;
  }

  return arange;
}
/* ------- end   --------------------------   intrange.c -------- --- */

/* ------- begin -------------------------- finish_jobs.c ------- --- */
void finish_jobs(void)
/* Frees from memory stuff used for job control */
{

  /* Get total number of tasks and convergence statuses */ 
  MPI_Allreduce(MPI_IN_PLACE, &mpi.Ntasks,  1, MPI_LONG, MPI_SUM, mpi.comm);
  MPI_Allreduce(MPI_IN_PLACE, &mpi.ncrash,  1, MPI_LONG, MPI_SUM, mpi.comm);
  MPI_Allreduce(MPI_IN_PLACE, &mpi.nconv,   1, MPI_LONG, MPI_SUM, mpi.comm);
  MPI_Allreduce(MPI_IN_PLACE, &mpi.nnoconv, 1, MPI_LONG, MPI_SUM, mpi.comm);

  free(mpi.xnum);
  free(mpi.ynum);
  freeMatrix((void **) mpi.taskmap);

}
/* ------- end   -------------------------- finish_jobs.c ------- --- */



/* ------- begin -------------------------- matrix_intl.c ----------- */

long **matrix_long(long Nrow, long Ncol)
{
  register long i;

  long *theMatrix, **Matrix;
  long typeSize = sizeof(long), pointerSize = sizeof(long *);

  theMatrix = (long *)  calloc(Nrow * Ncol, typeSize);
  Matrix    = (long **) malloc(Nrow * pointerSize);
  for (i = 0;  i < Nrow;  i++, theMatrix += Ncol)
    Matrix[i] = theMatrix;

  return Matrix;
}
/* ------- end ---------------------------- matrix_intl.c ----------- */