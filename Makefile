TOP_DIR = ../..
include $(TOP_DIR)/tools/Makefile.common

#
# TODO add build for mongodb
#
 # 3627  curl -O -L https://github.com/mongodb/mongo-c-driver/releases/download/1.16.2/mongo-c-driver-1.16.2.tar.gz
 # 3629  tar xzf mongo-c-driver-1.16.2.tar.gz 
 # 3702  cd mongo-c-driver-1.16.2
 # 3703  ls cmake-build/
 # 3704  cd cmake-build/
 # 3705  CXX=$d/bin/g++ CC=$d/bin/gcc cmake  ..  -DCMAKE_INSTALL_PREFIX=/home/olson/P3/dev-slurm/dev_container/modules/p4_ws/mongodb 
 # 3706  make -j16
 # 3707  make install

 # 3678  git clone https://github.com/mongodb/mongo-cxx-driver.git     --branch releases/stable --depth 1

 # 3712  cd mongo-cxx-driver/
 # 3713  cd build
 # 3714  CXX=$d/bin/g++ CC=$d/bin/gcc cmake  ..  -DCMAKE_INSTALL_PREFIX=/home/olson/P3/dev-slurm/dev_container/modules/p4_ws/mongodb   -DCMAKE_PREFIX_PATH=/home/olson/P3/dev-slurm/dev_container/modules/p4_ws/mongodb 
 # 3715  make -j12
 # 3716  make install
# cd mongodb; ln -s lib64 lib

TARGET ?= /kb/deployment
DEPLOY_TARGET ?= $(TARGET)
DEPLOY_RUNTIME ?= /disks/patric-common/runtime

SRC_SERVICE_PERL = $(wildcard service-scripts/*.pl)
BIN_SERVICE_PERL = $(addprefix $(BIN_DIR)/,$(basename $(notdir $(SRC_SERVICE_PERL))))
DEPLOY_SERVICE_PERL = $(addprefix $(SERVICE_DIR)/bin/,$(basename $(notdir $(SRC_SERVICE_PERL))))

STARMAN_WORKERS = 5

#DATA_API_URL = https://www.patricbrc.org/api
DATA_API_URL = https://p3.theseed.org/services/data_api
APP_SERVICE_URL = https://p3.theseed.org/services/app_service

BUILD_TOOLS = $(DEPLOY_RUNTIME)/gcc-9.3.0
CXX = $(BUILD_TOOLS)/bin/g++

# CXX_HANDLER_TRACKING = -DBOOST_ASIO_ENABLE_HANDLER_TRACKING

MONGODB_BASE = $(shell pwd)/mongodb
PKG_CONFIG_PATH = $(MONGODB_BASE)/lib64/pkgconfig
MONGODB_CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags libmongocxx)
MONGODB_LIBS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs libmongocxx)

BOOST = $(DEPLOY_RUNTIME)/boost-latest-valgrind

BOOST_JSON = -DBOOST_JSON_HEADER_ONLY -Ijson/include

INCLUDES = -I$(BOOST)/include $(BOOST_JSON) $(MONGODB_CFLAGS)
CXXFLAGS = $(INCLUDES) -g  -std=c++14 $(CXX_HANDLER_TRACKING) -DBOOST_USE_VALGRIND
CXX_LDFLAGS = -Wl,-rpath,$(BUILD_TOOLS)/lib64 -Wl,-rpath,$(BOOST)/lib -Wl,-rpath,$(MONGODB_BASE)/lib64
LDFLAGS = -L$(BOOST)/lib

LIBS = $(BOOST)/lib/libboost_system.a \
	$(BOOST)/lib/libboost_filesystem.a \
	$(BOOST)/lib/libboost_timer.a \
	$(BOOST)/lib/libboost_chrono.a \
	$(BOOST)/lib/libboost_coroutine.a \
	$(BOOST)/lib/libboost_context.a \
	$(BOOST)/lib/libboost_iostreams.a \
	$(BOOST)/lib/libboost_regex.a \
	$(BOOST)/lib/libboost_thread.a \
	$(BOOST)/lib/libboost_program_options.a \
	$(BOOST)/lib/libboost_system.a \
	$(MONGODB_LIBS) \
	-lpthread -lcrypto

ifdef AUTO_DEPLOY_CONFIG
CXX_DEFINES = -DAPP_SERVICE_URL='"$(APP_SERVICE_URL)"' -DDATA_API_URL='"$(DATA_API_URL)"' -DDEPLOY_LIBDIR='"$(TARGET)/lib"'
else
CXX_DEFINES = -DAPP_SERVICE_URL='"$(APP_SERVICE_URL)"' -DDATA_API_URL='"$(DATA_API_URL)"' -DDEPLOY_LIBDIR='"$(CURDIR)"'
endif

all: binaries

deploy-client:

deploy-service:

binaries: $(TOP_DIR)/bin/p4x-workspace

p4x-workspace: p4x-workspace.o WorkspaceDB.o WorkspaceService.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) $(CXX_DEFINES) -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS) -lssl -lcrypto

$(TOP_DIR)/bin/%: %
	cp $^ $@

WS_DEPS = WorkspaceState.h JSONRPC.h ServiceDispatcher.h WorkspaceService.h WorkspaceDB.h

depend: 
	makedepend *.cpp *.cc

SigningCerts.h: SigningCerts.h.tt load-signing-certs.pl
	$(DEPLOY_RUNTIME)/bin/perl load-signing-certs.pl

x: x.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)
ssl: ssl.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)

WorkspaceDB.o p4x-workspace.o: json/include/boost/json.hpp $(WS_DEPS)

json/include/boost/json.hpp: 
	git clone https://github.com/CPPAlliance/json

pidinfo: pidinfo.cc
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)

all: 

deploy: deploy-client deploy-service
deploy-all: deploy-client deploy-service
deploy-client: 

deploy-service: 

include $(TOP_DIR)/tools/Makefile.common.rules

# DO NOT DELETE

p4x-workspace.o: WorkspaceService.h WorkspaceErrors.h WorkspaceState.h
p4x-workspace.o: DispatchContext.h JSONRPC.h WorkspaceDB.h
p4x-workspace.o: ServiceDispatcher.h
ssl.o: AuthToken.h SigningCerts.h /usr/include/openssl/bio.h
ssl.o: /usr/include/openssl/e_os2.h /usr/include/openssl/opensslconf.h
ssl.o: /usr/include/openssl/opensslconf-x86_64.h /usr/include/stdio.h
ssl.o: /usr/include/features.h /usr/include/sys/cdefs.h
ssl.o: /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h
ssl.o: /usr/include/gnu/stubs-64.h /usr/include/bits/types.h
ssl.o: /usr/include/bits/typesizes.h /usr/include/libio.h
ssl.o: /usr/include/_G_config.h /usr/include/wchar.h
ssl.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
ssl.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
ssl.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
ssl.o: /usr/include/endian.h /usr/include/bits/endian.h
ssl.o: /usr/include/bits/byteswap.h /usr/include/sys/types.h
ssl.o: /usr/include/time.h /usr/include/sys/select.h
ssl.o: /usr/include/bits/select.h /usr/include/bits/sigset.h
ssl.o: /usr/include/bits/time.h /usr/include/sys/sysmacros.h
ssl.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
ssl.o: /usr/include/openssl/stack.h /usr/include/openssl/safestack.h
ssl.o: /usr/include/openssl/opensslv.h /usr/include/openssl/ossl_typ.h
ssl.o: /usr/include/openssl/symhacks.h /usr/include/openssl/pem.h
ssl.o: /usr/include/openssl/evp.h /usr/include/openssl/fips.h
ssl.o: /usr/include/openssl/objects.h /usr/include/openssl/obj_mac.h
ssl.o: /usr/include/openssl/asn1.h /usr/include/openssl/bn.h
ssl.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
ssl.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
ssl.o: /usr/include/bits/posix2_lim.h /usr/include/openssl/x509.h
ssl.o: /usr/include/openssl/buffer.h /usr/include/openssl/ec.h
ssl.o: /usr/include/openssl/ecdsa.h /usr/include/openssl/ecdh.h
ssl.o: /usr/include/openssl/rsa.h /usr/include/openssl/dsa.h
ssl.o: /usr/include/openssl/dh.h /usr/include/openssl/sha.h
ssl.o: /usr/include/openssl/x509_vfy.h /usr/include/openssl/lhash.h
ssl.o: /usr/include/openssl/pkcs7.h /usr/include/openssl/pem2.h
ssl.o: /usr/include/openssl/err.h /usr/include/errno.h
ssl.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
ssl.o: /usr/include/asm/errno.h /usr/include/asm-generic/errno.h
ssl.o: /usr/include/asm-generic/errno-base.h /usr/include/openssl/conf.h
WorkspaceDB.o: WorkspaceDB.h WorkspaceService.h WorkspaceErrors.h
WorkspaceDB.o: WorkspaceState.h DispatchContext.h JSONRPC.h
WorkspaceService.o: WorkspaceService.h WorkspaceErrors.h WorkspaceState.h
WorkspaceService.o: DispatchContext.h JSONRPC.h WorkspaceDB.h
