############################################################################
#             Makefile to build the GTC code
#           ==================================
#
# You only need to type "gmake" to build the code on the platforms
# defined below. The makefile runs the "uname -s" command to detect
# the operating system automatically. By default, this makefile
# uses OPENMP=y, 64bits=y, and ESSL=y, which are the settings for
# most runs on the IBM SP (AIX operating system). The executable will
# then be called "gtc". On platforms without OpenMP support or if
# OPENMP=n, the executable is called "gtcmpi".
#
# Other options are:
#
#  % gmake OPENMP=y       Builds the code with OpenMP support
#  % gmake OPENMP=n       Builds the code WITHOUT OpenMP support
#
#  % gmake DOUBLE_PRECISION=y  Builds with 8-byte floating point precision
#
#  % gmake DEBUG=y        Compiles the files with debug option (-g)
#                         The default is no debug option
#
#  % gmake ESSL=y         On AIX, uses the FFT routine from ESSL library
#  % gmake ESSL=n         On AIX, uses the FFT routine from the NAG library
#                         The default is to use the NAG library routines
#                         on all other platforms
#
#  % gmake 64BITS=y       To compile a 64-bit version on AIX
#  % gmake 64BITS=n       To compile a 32-bit version on AIX
#                         The default is 32 bits on Linux clusters
#
#  % gmake PGI=y          Use the PGI compiler (pgf90) on Linux. The default
#                         is to use the Lahey-Fujitsu compiler lf95.
#
#  % gmake ALTIX=y        Compiles with Intel compilers on the Altix
#                         using ifort ... -lmpi
#
# You can combine more than one option on the command line:
#
#  % gmake OPENMP=y ESSL=y
#
# Special targets:
#
#  % gmake clean      Removes the executable and all object files (*.o)
#
#  % gmake cleanomp   Removes the executable and the object files
#                     containing OpenMP directives
#
#  % gmake doc        Rebuilds the documentation.
#
#############################################################################
64BITS=y
XT3=y

# Default names of some platform-dependent files
SETUP:=setup.o
CHARGEI:=chargei.o
PUSHI:=pushi.o
POISSON:=poisson.o
SHIFTI:=shifti.o
OPENMP=y
INTEL=y
# Default executable name on machines without OpenMP support
CMD:=gtc
LIB:=

CC=mpicxx

# In the next declaration, "uname" is a standard Unix command which prints
# the name of the current system.
os:= $(shell uname -s)

# Common file for fft routine using the Glassman FFT source code.
FFT:=fft_gl.o

# We initialize to an empty string the variable that contains the compiler
# option for the OpenMP directives.
#OMPOPT:=-openmp



# Settings for Linux platform. The default is to use the LF95 compiler
ifeq ($(os),Linux)
  # Flags for the Linux system
  # Default compiler and options: Lahey-Fujitsu compiler
    CMP:=mpif90
    #OPT:=-D_USENVRAM
    #F90C:=/opt/intel/bin/ifort
    ##OPT:=-O --ap --tpp --ntrace --staticlink -I/usr/local/lff95/include
  ##  OPT:=-O --ap --pca --trace
 # YX add the following netcdf lib
    NETCDF := -lnetcdf  -lnetcdff 
    LIB := -L/usr/lib \
               -I/usr/include $(NETCDF)
  ifeq ($(PGI),y)
    MPIMODULE:=/usr/pppl/pgi/5.2-1/mpich-1.2.6/include/f90base
    F90C:=pgf90
    OPT:=-O -D__PGF90 -Mfree -Kieee 
 #   LIB:=
  endif
 ### ifeq ($(XT3),y)
 ###   CMP:=ftn
 ###   F90C:=ftn
 ###   NETCDFDIR:=/apps/netcdf/3.6.0/xt3_pgi605
 ###   OPT:=-fastsse -I$(NETCDFDIR)/include
    #OPT:=-fastsse ${HDF5_FLIB}
 ###   LIB:=${HDF5_FLIB} -L$(NETCDFDIR)/lib -lnetcdf
 ### endif
  ifeq ($(INTEL),y)
    F90C:=ifort
    OPT:=-O
  endif
  ifeq ($(PATHSCALE),y)
    F90C:=pathf90
    #OPT:=-O3 -static
    OPT:=-O3
  endif
  ifeq ($(ALTIX),y)
    CMP:=ifort
    ###OPT:=-O3 -ipo -ftz -stack_temps
    OPT:=-O3 -ftz-
    #OPT:=-O3 -ipo -ftz -stack_temps
    ###OPT:=-O -g -ftz
    LIB:=-lmpi
  endif
  ifeq ($(DOUBLE_PRECISION),y)
      OPT:=-DDOUBLE_PRECISION $(OPT)
  endif
  ifeq ($(DEBUG),y)
     OPT:=-g $(OPT)
  endif
endif



##################################################################
# We add ".F90" to the list of suffixes to allow source files on which the
# co-processor will be run automatically.
.SUFFIXES: .o .f90 .F90

INCLUDE=$(NVMALLOC_HOME)
src_path=$(NVMALLOC_HOME)
LIB_PATH=$(NVMALLOC_HOME)

CFLAGS := -g -I$(INCLUDE) -Wall -pipe -fvisibility=hidden \
      -funroll-loops  -Wno-implicit -Wno-uninitialized \
      -Wno-unused-function -Wno-unused-variable  -lm -lssl -lcrypt  -fPIC

NVLIBS= -lpthread -lm  -lssl -lcrypt  #-larmci #-lsnappy


# See source code comments to avoid memory leaks when enabling MALLOC_MAG.
#CPPFLAGS := -DMALLOC_PRODUCTION -DMALLOC_MAG
CPPFLAGS := -DMALLOC_PRODUCTION -lstdc++
#CPPFLAGS := $(CPPFLAGS) -D_COMPARE_PAGES 
#CPPFLAGS := $(CPPFLAGS)	 -D_FAULT_STATS 
#CPPFLAGS := $(CPPFLAGS) -D_ASYNC_RMT_CHKPT
#CPPFLAGS := $(CPPFLAGS) -D_ASYNC_LCL_CHK
#CPPFLAGS := $(CPPFLAGS) -D_NVSTATS
#CPPFLAGS := $(CPPFLAGS) -D_USE_FAKE_NVMAP
#CPPFLAGS := $(CPPFLAGS) -D_ARMCI_CHECKPOINT



#OPT:= -cpp -D_NOCHECKPOINT $(OPT)  -D_GTC_STATS
OPT:=  -cpp $(OPT) #-D_USENVRAM
#OPT:= -DVALIDATE_CHKSM -cpp $(OPT)
#OPT:= -cpp $(OPT) 
#OPT:= -cpp -D_USESCR $(OPT) -lscrf
#OPT:= $(OPT) -D_GTC_STATS
#OPT:= $(OPT) -D_COMPARE_PAGES -lsnappy
#OPT:= -cpp $(OPT) -D_SYNTHETIC
#OPT:= -cpp $(OPT) -D_ASYNC_LCL_CHK
#OPT:= -cpp $(OPT) -D_ARMCI_CHECKPOINT

#OPT:= $(OPT) -D_USE_FAKE_NVMAP -D_ASYNC_RMT_CHKPT


JEMALLOC_OBJS=  $(src_path)/jemalloc.o $(src_path)/arena.o $(src_path)/atomic.o \
                $(src_path)/base.o $(src_path)/ckh.o $(src_path)/ctl.o $(src_path)/extent.o \
                $(src_path)/hash.o $(src_path)/huge.o $(src_path)/mb.o \
                $(src_path)/mutex.o $(src_path)/prof.o $(src_path)/quarantine.o \
                $(src_path)/rtree.o $(src_path)/stats.o $(src_path)/tcache.o \
                $(src_path)/util.o $(src_path)/tsd.o $(src_path)/chunk.o \
                $(src_path)/bitmap.o $(src_path)/chunk_mmap.o $(src_path)/chunk_dss.o\
		$(src_path)/gtthread_spinlocks.o \
		$(src_path)/rbtree.o $(src_path)/nv_map.o $(src_path)/checkpoint.o	\
		$(src_path)/util_func.o #$(src_path)/nv_rmtckpt.o 
		#$(src_path)/armci_checkpoint.o \
#$(src_path)/zone.o


# List of all the object files needed to build the code
OBJ:=allocate.o module.o main.o function.o $(SETUP) ran_num_gen.o set_random_values.o \
    load.o restart.o diagnosis.o snapshot.o $(CHARGEI) $(POISSON) smooth.o \
    field.o $(PUSHI) $(SHIFTI) $(FFT) tracking.o \
    dataout3d.o  #c_io.o $(JEMALLOC_OBJS)



## mem_check.o \
##    output3d_serial.o output.o

# selectmode.o volume.o 
CC=mpicc
CXX=mpicxx
$(CMD): $(OBJ)
	$(CMP) $(OMPOPT) $(OPT) -o $(CMD) $(OBJ) $(LIB) $(CPPFLAGS) $(CFLAGS) #$(NVLIBS)

#c_io.o: $(src_path)/c_io.c
#		$(CC) -c  $(src_path)/c_io.c $(CFLAGS) $(NVLIBS) $(CPPFLAGS) #$(src_path)/nv_rmtckpt.o 

#$(src_path)/nv_map.o: $(src_path)/nv_map.cc 
#	$(CXX) -c $(src_path)/nv_map.cc -o $(src_path)/nv_map.o $(NVLIBS) $(CPPFLAGS)

#$(src_path)/rbtree.o: $(src_path)/rbtree.cc 
#	$(CXX) -c $(src_path)/rbtree.cc -o $(src_path)/rbtree.o $(NVLIBS) $(CPPFLAGS)

#$(src_path)/util_func.o: $(src_path)/util_func.cc 
#	$(CXX) -c $(src_path)/util_func.cc -o $(src_path)/util_func.o $(NVLIBS) $(CPPFLAGS)


#$(src_path)/checkpoint.o: $(src_path)/checkpoint.cc
#	$(CXX) -c $(src_path)/checkpoint.cc -o  $(src_path)/checkpoint.o  $(NVLIBS) $(CPPFLAGS)

#$(src_path)/armci_checkpoint.o: $(src_path)/armci_checkpoint.cc
#	$(CXX) -c $(src_path)/armci_checkpoint.cc -o  $(src_path)/armci_checkpoint.o  $(NVLIBS) $(CPPFLAGS)



module.o : module.F90
	$(CMP) $(OMPOPT) $(OPT) -c module.F90

#$(OBJ): module.o

track_analysis: module.o track_analysis.o
	$(F90C) $(OMPOPT) $(OPT) -o $@ $^

##output3d_serial.o: output3d_serial.f90
##	$(CMP) $(OPT) ${HDF5_FLIB} -c output3d_serial.f90
dataout3d.o: dataout3d.f90
	$(CMP) $(OPT) ${LIB} -c dataout3d.f90

restart.o: restart.f90
	$(CMP) $(OPT) ${LIB} -c restart.f90

##mem_check.o: mem_check.c
##	cc -fastsse -c mem_check.c

#c_io.o: nvram_lib/c_io.c
#	 gcc -c -I/usr/include/mpi nvram_lib/c_io.c 

.f90.o : module.o
	$(CMP) $(OMPOPT) $(OPT) $(LIB) -c $<

.F90.o : module.o
	$(CMP) $(OMPOPT) $(OPT) $(LIB) -c $<

ncdpost: ncdpost.f90
	$(F90C) $(OPT) -o $@ $(LIB) $^

PHI_dat_to_ncd: PHI_dat_to_ncd.f90
	$(F90C) $(OPT) -o $@ $(LIB) $^

# The following tag is meant to "clean" the directory by removing the
# executable along with all the object files created by the compilation 
# One only has to run:  gmake clean

clean:
	rm -f $(CMD) $(OBJ) *.mod

