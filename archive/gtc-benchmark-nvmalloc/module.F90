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

module precision
  use mpi
  integer, parameter :: doubleprec=selected_real_kind(12),&
       singleprec=selected_real_kind(6),&
       defaultprec=kind(0.0)
#ifdef DOUBLE_PRECISION
  integer, parameter :: wp=doubleprec,mpi_Rsize=MPI_DOUBLE_PRECISION,&
                        mpi_Csize=MPI_DOUBLE_COMPLEX
#else
  integer, parameter :: wp=singleprec,mpi_Rsize=MPI_REAL,&
                        mpi_Csize=MPI_COMPLEX
#endif

end module precision

module global_parameters
  use precision
  integer,parameter :: ihistory=22,snapout=33,maxmpsi=192
  integer mi,mimax,me,me1,memax,mgrid,mpsi,mthetamax,mzeta,mzetamax,&
       istep,ndiag,ntracer,msnap,mstep,mstepall,stdout,mype,numberpe,&
       mode00,nbound,irun,iload,irk,idiag,ncycle,mtdiag,idiag1,idiag2,&
       ntracer1,nhybrid,ihybrid,nparam,rng_control
  real(wp) nonlinear,paranl,a0,a1,a,q0,q1,q2,pi,tstep,kappati,kappate,kappan,&
       flow0,flow1,flow2,ulength,utime,gyroradius,deltar,deltaz,zetamax,&
       zetamin,umax,tite,rc,rw,tauii,qion,qelectron,aion,aelectron
  integer,dimension(:),allocatable :: mtheta
  real(wp),dimension(:),allocatable :: deltat
   
  !NVRAM changes
  !integer usenvram  
  !usenvram = 0

#ifdef _SX
! SX-6 trick to minimize bank conflict in chargei
  integer,dimension(0:maxmpsi) :: mmtheta
!cdir duplicate(mmtheta,1024)
#endif

end module global_parameters

module particle_array
  use precision
  integer,dimension(:),allocatable :: kzion,kzelectron,jtelectron0,jtelectron1

#ifdef _SYNTHETIC

  integer, pointer :: jtion0(:,:),jtion1(:,:)      

  real, pointer :: wpion(:,:), wtion0(:,:),wtion1(:,:)

  real(wp),dimension(:),allocatable :: wzion,wzelectron,wpelectron,&
       wtelectron0,wtelectron1
#else
  integer,dimension(:,:),allocatable :: jtion0,jtion1

  real(wp),dimension(:),allocatable :: wzion,wzelectron,wpelectron,&
       wtelectron0,wtelectron1
  real(wp),dimension(:,:),allocatable :: wpion,wtion0,wtion1

#endif


#ifndef _USENVRAM

	 real(wp),dimension(:,:),allocatable :: zion,zion0,zelectron,zelectron0,zelectron1
#else
  	real, pointer :: zion(:,:) 
	real, pointer :: zion0(:,:)
	real, pointer :: zelectron(:,:)
	real, pointer :: zelectron0(:,:)

#ifdef _SYNTHETIC
         real, pointer :: zelectron1(:,:)
#else
	real(wp),dimension(:,:),allocatable ::zelectron1
#endif

#endif


end module particle_array

module particle_tracking
  use precision
  integer track_particles,nptrack,isnap
  real(wp),dimension(:,:,:),allocatable :: ptracked
  integer,dimension(:),allocatable :: ntrackp
end module particle_tracking

module field_array
  use precision
  !integer,parameter :: mmpsi=192
  !integer,dimension(:),allocatable :: itran,igrid
  !integer,dimension(:,:,:),allocatable :: jtp1,jtp2
  !real(wp),dimension(:),allocatable :: phi00,phip00,rtemi,rteme,rden,qtinv,&
   !    pmarki,pmarke,zonali,zonale,gradt
  !real(wp),dimension(:,:),allocatable :: phi,densityi,densitye,markeri,&
   !    markere,pgyro,tgyro,dtemper,heatflux,phit

  integer,parameter :: mmpsi=192
  integer,dimension(:),allocatable :: itran,igrid

#ifdef _SYNTHETIC
     integer, pointer :: jtp1(:,:,:)
     integer,dimension(:,:,:),allocatable :: jtp2
#else
  integer,dimension(:,:,:),allocatable :: jtp1,jtp2
#endif

  real(wp),dimension(:),allocatable :: phi00,rtemi,rteme,rden,qtinv,&
       pmarki,pmarke,gradt


#ifdef _SYNTHETIC
  real(wp),dimension(:,:),allocatable :: densityi,markeri,&
       pgyro,tgyro,dtemper,heatflux
        
        real, pointer :: phit(:,:)
        real, pointer :: densitye(:,:)
        real, pointer :: markere(:,:)
#else
  real(wp),dimension(:,:),allocatable :: densityi,densitye,markeri,&
       markere,pgyro,tgyro,dtemper,heatflux,phit
#endif


   real, pointer :: phip00(:) 
   real, pointer :: zonali(:)
   real, pointer :: zonale(:)
   real, pointer :: phi(:,:)

  !NVRAM changes
  !real(wp),dimension(:,:,:),allocatable :: evector,wtp1,wtp2,phisave
  real(wp),dimension(:,:,:),allocatable :: evector,wtp1,wtp2
  real, pointer :: phisave(:,:,:)


  real(wp) :: Total_field_energy(3)

#ifdef _SX
! SX-6 trick to minimize bank conflicts in chargei
!cdir duplicate(iigrid,1024)
  integer,dimension(0:mmpsi) :: iigrid
!cdir duplicate(qqtinv,1024)
  real(wp),dimension(0:mmpsi) :: qqtinv
#endif

end module field_array

module diagnosis_array
  use precision
  integer,parameter :: mflux=5,num_mode=8,m_poloidal=9
  integer nmode(num_mode),mmode(num_mode)
  real(wp) efluxi,efluxe,pfluxi,pfluxe,ddeni,ddene,dflowi,dflowe,&
       entropyi,entropye,efield,eradial,particles_energy(2),eflux(mflux),&
       rmarker(mflux),amp_mode(2,num_mode,2)
  !real(wp),dimension(:),allocatable :: hfluxpsi,hfluxpse,rdtemi,pfluxpsi,rdteme
  real(wp),dimension(:),allocatable :: hfluxpsi,hfluxpse
  real(wp),dimension(:,:,:),allocatable :: eigenmode
  real(wp) etracer,ptracer(4)


  real, pointer :: pfluxpsi(:), rdteme(:),rdtemi(:)





end module diagnosis_array

