PKGDIR          ?= ../..
L4DIR           ?= $(PKGDIR)/../..

TARGET          = libquadmath.a libquadmath.so
PC_FILENAMES	= libquadmath
SYSTEMS		= x86-l4f amd64-l4f

SRC_C += \
  $(addprefix math/, \
    acoshq.c   clog10q.c      fabsq.c      lgammaq.c     roundq.c \
    acosq.c    clogq.c        fdimq.c      llrintq.c     scalblnq.c \
    asinhq.c   complex.c      finiteq.c    llroundq.c    scalbnq.c \
    asinq.c    conjq.c        floorq.c     log10q.c      signbitq.c \
    atan2q.c   copysignq.c    fmaq.c       log1pq.c      sincos_table.c \
    atanhq.c   coshq.c        fmaxq.c      log2q.c       sincosq.c \
    atanq.c    cosq.c         fminq.c      logq.c        sincosq_kernel.c \
    cacoshq.c  cosq_kernel.c  fmodq.c      lrintq.c      sinhq.c \
    cacosq.c   cprojq.c       frexpq.c     lroundq.c     sinq.c \
    casinhq.c  crealq.c       hypotq.c     modfq.c       sinq_kernel.c \
    casinq.c   csinhq.c       ilogbq.c     nanq.c        sqrtq.c \
    catanhq.c  csinq.c        isinf_nsq.c  nearbyintq.c  tanhq.c \
    catanq.c   csqrtq.c       isinfq.c     nextafterq.c  tanq.c \
    cbrtq.c    ctanhq.c       isnanq.c     powq.c        tgammaq.c \
    ccoshq.c   ctanq.c        j0q.c        rem_pio2q.c   truncq.c \
    ceilq.c    erfq.c         j1q.c        remainderq.c  x2y2m1q.c \
    cexpq.c    expm1q.c       jnq.c        remquoq.c \
    cimagq.c   expq.c         ldexpq.c     rintq.c)


include $(L4DIR)/mk/Makeconf

#REQUIRES_LIBS = libc_be_sig libc_support_misc libstdc++ libc

CONTRIB_DIR     = $(SRC_DIR)/../contrib
PRIVATE_INCDIR  = $(SRC_DIR) $(CONTRIB_DIR)

vpath %.c   $(CONTRIB_DIR)

include $(L4DIR)/mk/lib.mk
