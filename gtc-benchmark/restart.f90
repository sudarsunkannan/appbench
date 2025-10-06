! Copyright 2008 Z. Lin <zhihongl@uci.edu>
!
! This file is part of GTC version 1.
!
! GTC version 1 is free software: you can redistribute it and/or modify
! it under the terms of the GNU General Public License as published by
! the Free Software Foundation, either version 3 of the License, or
! (at your option) any later version.
!
! GTC version 1 is distributed in the hope that it will be useful,
! but WITHOUT ANY WARRANTY; without even the implied warranty of
! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
! GNU General Public License for more details.
!
! You should have received a copy of the GNU General Public License
! along with GTC version 1.  If not, see <http://www.gnu.org/licenses/>.

subroutine restart_write
  use global_parameters
  use particle_array
  use field_array
  use diagnosis_array
  implicit none

  character(len=18) cdum
  character(len=10) restart_dir
  character(len=60) file_name
  real(wp) dum
  integer i,j,mquantity,mflx,n_mode,mstepfinal,noutputs
  integer save_restart_files,ierr
  
  integer filesize, valid, writeunit
  character*1024 fname_tmp, file_suffix, fname
  character*1024 :: basefname = "rank_"
  
  valid = 1;


  !save_restart_files=1
  save_restart_files=0

!!!!!!!!!!!!!!!******************
!!  if(mype < 10)then
!!     write(cdum,'("DATA_RESTART.00",i1)')mype
!!   elseif(mype < 100)then
!!     write(cdum,'("DATA_RESTART.0",i2)')mype
!!  else
!!     write(cdum,'("DATA_RESTART.",i3)')mype
!!  endif
!!!!!!!!!!!!!************************

#ifdef _USENVRAM1

      call nvchkpt_all(mype);
      write(222)mi,me,ntracer 
      if(mype==0)write(222)etracer,ptracer
#else


  if(mype < 10)then
     write(cdum,'("DATA_RESTART.0000",i1)')(mype)
  elseif(mype < 100)then
     write(cdum,'("DATA_RESTART.000",i2)')(mype)
  elseif(mype < 1000)then
     write(cdum,'("DATA_RESTART.00",i3)')(mype)
  elseif(mype < 10000)then
     write(cdum,'("DATA_RESTART.0",i4)')(mype)
  else
     write(cdum,'("DATA_RESTART.",i5)')(mype)
  endif 

#ifdef _USESCR
    writeunit = mype
    write(file_suffix, '(i5.5)') writeunit
    fname_tmp = trim(basefname) // trim(file_suffix) // ".ckpt"
    fname = trim(basefname) // trim(file_suffix) // ".ckpt"
   call SCR_ROUTE_FILE(fname_tmp, fname, ierr)
    valid = 1
    open(unit=writeunit,file=fname,form='unformatted', action='write')

      write(writeunit)mi,me,ntracer,rdtemi,rdteme,pfluxpsi,phi,phip00,zonali,zonale
      if(mype==0)write(writeunit)etracer,ptracer
      write(writeunit)zion(1:nparam,1:mi),zion0(6,1:mi)
      if(nhybrid>0)write(writeunit)phisave,zelectron(1:6,1:me),zelectron0(6,1:me)
      close(writeunit)

#else
  if(save_restart_files==1)then
     write(restart_dir,'("STEP_",i0)')(mstepall+istep)
     !if(mype==0)
     call system("mkdir "//restart_dir)
     call MPI_BARRIER(MPI_COMM_WORLD,ierr)
     file_name=trim(restart_dir)//'/'//trim(cdum)
     open(222,file=file_name,status='replace',form='unformatted')
  else
     open(222,file=cdum,status='replace',form='unformatted')
  endif

      write(222)mi,me,ntracer,rdtemi,rdteme,pfluxpsi,phi,phip00,zonali,zonale
      if(mype==0)write(222)etracer,ptracer

#ifdef _SYNTHETIC
     write(222)zion(1:nparam,1:mi),zion0(1:nparam,1:mi)
     write(222)jtion0(1:nparam,1:mi),jtion1(1:nparam,1:mi),wpion(1:nparam,1:mi),wtion0(1:nparam,1:mi),wtion1(1:nparam,1:mi)
     if(nhybrid>0)write(222)zelectron(1:nparam,1:mi),zelectron0(1:nparam,1:mi),zelectron1(1:nparam,1:mi)

#else
      write(222)zion(1:nparam,1:mi),zion0(6,1:mi)
      if(nhybrid>0)write(222)phisave,zelectron(1:6,1:me),zelectron0(6,1:me)

!SYNTHETIC
#endif

       close(222)
#endif
    !filesize =nparam*mimax 
    !call write_io(cdum, zion, filesize,1)
    !call write_io(cdum, zion0, filesize,0)
    !call write_io(cdum, zonali, mpsi,0)
    !call write_io(cdum, zonale, mpsi,0)
    !call write_io(cdum, phip00, mpsi,0)
    !call write_io(cdum, pfluxpsi, mpsi,0)
    !call write_io(cdum, rdteme, mpsi,0)
    !call write_io(cdum, rdtemi, mpsi,0)
    !filesize = mzeta * mgrid
    ! call write_io(cdum, phi, filesize,0)
    !close(222) 
    !return 



! S.Ethier 01/30/04 Save a copy of history.out and sheareb.out for restart
  if(mype==0 .and. istep<=mstep)then
     open(777,file='history_restart.out',status='replace')
     rewind(ihistory)
     read(ihistory,101)j
     write(777,101)j
     read(ihistory,101)mquantity
     write(777,101)mquantity
     read(ihistory,101)mflx
     write(777,101)mflx
     read(ihistory,101)n_mode
     write(777,101)n_mode
     read(ihistory,101)mstepfinal
     noutputs=mstepfinal-mstep/ndiag+istep/ndiag
     write(777,101)noutputs
     do i=0,(mquantity+mflx+4*n_mode)*noutputs
        read(ihistory,102)dum
        write(777,102)dum
     enddo
     close(777)

   ! Now do sheareb.out
     open(777,file='sheareb_restart.out',status='replace')
     if(istep==mstep)open(444,file='sheareb.out',status='old')
     rewind(444)
     read(444,101)j
     write(777,101)j
     read(444,101)mflx
     write(777,101)mflx

     do i=1,mpsi*noutputs*j
        read(444,102)dum
        write(777,102)dum
      enddo
     close(777)
     if(istep==mstep)close(444)
  endif


#endif


101 format(i6)
102 format(e12.6)

end subroutine restart_write

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

subroutine restart_read
  use global_parameters
  use particle_array
  use field_array
  use diagnosis_array
  implicit none

  integer m
  character(len=18) cdum
  character(len=18) varname



!!!!********************************************
  
!!  if(mype < 10)then
!!     write(cdum,'("DATA_RESTART.00",i1)')mype
!!  elseif(mype < 100)then
!!     write(cdum,'("DATA_RESTART.0",i2)')mype
!!  else
!!     write(cdum,'("DATA_RESTART.",i3)')mype
!!  endif


!!!!!!!****************************************

  if(mype < 10)then
     write(cdum,'("DATA_RESTART.0000",i1)')mype
  elseif(mype < 100)then
     write(cdum,'("DATA_RESTART.000",i2)')mype
  elseif(mype < 1000)then
     write(cdum,'("DATA_RESTART.00",i3)')mype
  elseif(mype < 10000)then
     write(cdum,'("DATA_RESTART.0",i4)')mype
  else
     write(cdum,'("DATA_RESTART.",i5)')mype
  endif

  open(333,file=cdum,status='old',form='unformatted')

! read particle information to restart previous run
  !if(usenvram == 0) then

#ifdef _USENVRAM1
    read(333)mi,me,ntracer
    if(mype==0)read(333)etracer,ptracer
#else
    read(333)mi,me,ntracer,rdtemi,rdteme,pfluxpsi,phi,phip00,zonali,zonale
    read(333)zion(1:nparam,1:mi),zion0(6,1:mi)
    if(nhybrid>0)read(333)phisave,zelectron(1:6,1:me),zelectron0(6,1:me)
#endif
  close(333)

  print *,"USING RESTART CODE"

  return

! test domain decomposition
  do m=1,mi
     if(zion(3,m)>zetamax+1.0e-10 .or. zion(3,m)<zetamin-1.0e-10)then
        print *, 'PE=',mype, ' m=',m, ' zion=',zion(3,m)
        stop
     endif
  enddo
  if(nhybrid>0)then
     do m=1,me
        if(zelectron(3,m)>zetamax+1.0e-10 .or. zelectron(3,m)<zetamin-1.0e-10)then
           print *, 'PE=',mype, ' m=',m, ' zelectron=',zelectron(3,m)
           stop
        endif
     enddo
  endif


end subroutine restart_read

