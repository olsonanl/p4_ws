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
#  -DCMAKE_PREFIX_PATH=/disks/patric-common/runtime/ssl_1.1.1g
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

MONGODB_BASE = $(shell pwd)/mongodb-ssl-1.1
PKG_CONFIG_PATH = $(MONGODB_BASE)/lib64/pkgconfig
MONGODB_CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags libmongocxx)
MONGODB_LIBS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs libmongocxx)

BOOST = $(DEPLOY_RUNTIME)/boost-latest-valgrind

BOOST_JSON = -DBOOST_JSON_HEADER_ONLY -Ijson/include

OPENSSL = /disks/patric-common/runtime/ssl_1.1.1g
OPENSSL_INCLUDE = -I$(OPENSSL)/include
OPENSSL_LDFLAGS = -Wl,-rpath,$(OPENSSL)/lib
OPENSSL_LIBS = -L$(OPENSSL)/lib -lcrypto -lssl

INCLUDES = -I. -I$(BOOST)/include $(BOOST_JSON) $(MONGODB_CFLAGS) -Iinih $(OPENSSL_INCLUDE)
CXXFLAGS = $(INCLUDES) -g  -std=c++14 $(CXX_HANDLER_TRACKING) -DBOOST_USE_VALGRIND
CXX_LDFLAGS = -Wl,-rpath,$(BUILD_TOOLS)/lib64 -Wl,-rpath,$(BOOST)/lib -Wl,-rpath,$(MONGODB_BASE)/lib64 $(OPENSSL_LDFLAGS)
LDFLAGS = -L$(BOOST)/lib

LIBS = $(BOOST)/lib/libboost_system.a \
	$(BOOST)/lib/libboost_log.a \
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
	-lpthread \
	$(OPENSSL_LIBS)

ifdef AUTO_DEPLOY_CONFIG
CXX_DEFINES = -DAPP_SERVICE_URL='"$(APP_SERVICE_URL)"' -DDATA_API_URL='"$(DATA_API_URL)"' -DDEPLOY_LIBDIR='"$(TARGET)/lib"'
else
CXX_DEFINES = -DAPP_SERVICE_URL='"$(APP_SERVICE_URL)"' -DDATA_API_URL='"$(DATA_API_URL)"' -DDEPLOY_LIBDIR='"$(CURDIR)"'
endif

TESTS_SRC = $(wildcard tests/*.cpp)
TESTS = $(patsubst %.cpp,%,$(TESTS_SRC))

all: binaries

deploy-client:

deploy-service:

binaries: $(TOP_DIR)/bin/p4x-workspace

p4x-workspace: p4x-workspace.o WorkspaceDB.o WorkspaceService.o Logging.o ServiceConfig.o Shock.o UserAgent.o WorkspaceConfig.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) $(CXX_DEFINES) -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS) -lssl -lcrypto

$(TOP_DIR)/bin/%: %
	cp $^ $@

depend: 
	makedepend *.cpp *.cc tests/*.cpp

tests: $(TESTS)
	echo $(TESTS)

tests/%: tests/%.o 
	$(CXX) -I. -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS) $(BOOST)/lib/libboost_unit_test_framework.a -lrt

SigningCerts.h: SigningCerts.h.tt load-signing-certs.pl
	$(DEPLOY_RUNTIME)/bin/perl load-signing-certs.pl

x: x.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)
y: y.o UserAgent.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)
ssl: ssl.o
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)

json/include/boost/json.hpp: 
	git clone https://github.com/CPPAlliance/json

inih/INIReader.h:
	git clone https://github.com/jtilly/inih

pidinfo: pidinfo.cc
	PATH=$(BUILD_TOOLS)/bin:$$PATH $(CXX) -DPIDINFO_TEST_MAIN -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(CXX_LDFLAGS) $(LIBS)

all: 

deploy: deploy-client deploy-service
deploy-all: deploy-client deploy-service
deploy-client: 

deploy-service: 

include $(TOP_DIR)/tools/Makefile.common.rules

# DO NOT DELETE

Logging.o: Logging.h
p4x-workspace.o: WorkspaceTypes.h Logging.h WorkspaceService.h
p4x-workspace.o: WorkspaceErrors.h WorkspaceState.h AuthToken.h
p4x-workspace.o: SigningCerts.h /usr/include/openssl/bio.h
p4x-workspace.o: /usr/include/openssl/e_os2.h
p4x-workspace.o: /usr/include/openssl/opensslconf.h
p4x-workspace.o: /usr/include/openssl/opensslconf-x86_64.h
p4x-workspace.o: /usr/include/stdio.h /usr/include/features.h
p4x-workspace.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
p4x-workspace.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h
p4x-workspace.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
p4x-workspace.o: /usr/include/libio.h /usr/include/_G_config.h
p4x-workspace.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
p4x-workspace.o: /usr/include/bits/sys_errlist.h
p4x-workspace.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
p4x-workspace.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
p4x-workspace.o: /usr/include/endian.h /usr/include/bits/endian.h
p4x-workspace.o: /usr/include/bits/byteswap.h /usr/include/sys/types.h
p4x-workspace.o: /usr/include/time.h /usr/include/sys/select.h
p4x-workspace.o: /usr/include/bits/select.h /usr/include/bits/sigset.h
p4x-workspace.o: /usr/include/bits/time.h /usr/include/sys/sysmacros.h
p4x-workspace.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
p4x-workspace.o: /usr/include/openssl/stack.h
p4x-workspace.o: /usr/include/openssl/safestack.h
p4x-workspace.o: /usr/include/openssl/opensslv.h
p4x-workspace.o: /usr/include/openssl/ossl_typ.h
p4x-workspace.o: /usr/include/openssl/symhacks.h /usr/include/openssl/pem.h
p4x-workspace.o: /usr/include/openssl/evp.h /usr/include/openssl/fips.h
p4x-workspace.o: /usr/include/openssl/objects.h
p4x-workspace.o: /usr/include/openssl/obj_mac.h /usr/include/openssl/asn1.h
p4x-workspace.o: /usr/include/openssl/bn.h /usr/include/limits.h
p4x-workspace.o: /usr/include/bits/posix1_lim.h /usr/include/bits/local_lim.h
p4x-workspace.o: /usr/include/linux/limits.h /usr/include/bits/posix2_lim.h
p4x-workspace.o: /usr/include/openssl/x509.h /usr/include/openssl/buffer.h
p4x-workspace.o: /usr/include/openssl/ec.h /usr/include/openssl/ecdsa.h
p4x-workspace.o: /usr/include/openssl/ecdh.h /usr/include/openssl/rsa.h
p4x-workspace.o: /usr/include/openssl/dsa.h /usr/include/openssl/dh.h
p4x-workspace.o: /usr/include/openssl/sha.h /usr/include/openssl/x509_vfy.h
p4x-workspace.o: /usr/include/openssl/lhash.h /usr/include/openssl/pkcs7.h
p4x-workspace.o: /usr/include/openssl/pem2.h /usr/include/openssl/err.h
p4x-workspace.o: /usr/include/errno.h /usr/include/bits/errno.h
p4x-workspace.o: /usr/include/linux/errno.h /usr/include/asm/errno.h
p4x-workspace.o: /usr/include/asm-generic/errno.h
p4x-workspace.o: /usr/include/asm-generic/errno-base.h
p4x-workspace.o: /usr/include/openssl/conf.h WorkspaceConfig.h
p4x-workspace.o: ServiceConfig.h Shock.h parse_url.h UserAgent.h Base64.h
p4x-workspace.o: ServiceDispatcher.h JSONRPC.h WorkspaceDB.h
p4x-workspace.o: DispatchContext.h RootCertificates.h
p4x-workspace.o: /usr/include/openssl/x509v3.h
ServiceConfig.o: ServiceConfig.h
Shock.o: Shock.h AuthToken.h parse_url.h
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
UserAgent.o: UserAgent.h parse_url.h
WorkspaceConfig.o: WorkspaceService.h WorkspaceErrors.h WorkspaceState.h
WorkspaceConfig.o: AuthToken.h SigningCerts.h /usr/include/openssl/bio.h
WorkspaceConfig.o: /usr/include/openssl/e_os2.h
WorkspaceConfig.o: /usr/include/openssl/opensslconf.h
WorkspaceConfig.o: /usr/include/openssl/opensslconf-x86_64.h
WorkspaceConfig.o: /usr/include/stdio.h /usr/include/features.h
WorkspaceConfig.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
WorkspaceConfig.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h
WorkspaceConfig.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
WorkspaceConfig.o: /usr/include/libio.h /usr/include/_G_config.h
WorkspaceConfig.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
WorkspaceConfig.o: /usr/include/bits/sys_errlist.h
WorkspaceConfig.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
WorkspaceConfig.o: /usr/include/bits/waitflags.h
WorkspaceConfig.o: /usr/include/bits/waitstatus.h /usr/include/endian.h
WorkspaceConfig.o: /usr/include/bits/endian.h /usr/include/bits/byteswap.h
WorkspaceConfig.o: /usr/include/sys/types.h /usr/include/time.h
WorkspaceConfig.o: /usr/include/sys/select.h /usr/include/bits/select.h
WorkspaceConfig.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
WorkspaceConfig.o: /usr/include/sys/sysmacros.h
WorkspaceConfig.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
WorkspaceConfig.o: /usr/include/openssl/stack.h
WorkspaceConfig.o: /usr/include/openssl/safestack.h
WorkspaceConfig.o: /usr/include/openssl/opensslv.h
WorkspaceConfig.o: /usr/include/openssl/ossl_typ.h
WorkspaceConfig.o: /usr/include/openssl/symhacks.h /usr/include/openssl/pem.h
WorkspaceConfig.o: /usr/include/openssl/evp.h /usr/include/openssl/fips.h
WorkspaceConfig.o: /usr/include/openssl/objects.h
WorkspaceConfig.o: /usr/include/openssl/obj_mac.h /usr/include/openssl/asn1.h
WorkspaceConfig.o: /usr/include/openssl/bn.h /usr/include/limits.h
WorkspaceConfig.o: /usr/include/bits/posix1_lim.h
WorkspaceConfig.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
WorkspaceConfig.o: /usr/include/bits/posix2_lim.h /usr/include/openssl/x509.h
WorkspaceConfig.o: /usr/include/openssl/buffer.h /usr/include/openssl/ec.h
WorkspaceConfig.o: /usr/include/openssl/ecdsa.h /usr/include/openssl/ecdh.h
WorkspaceConfig.o: /usr/include/openssl/rsa.h /usr/include/openssl/dsa.h
WorkspaceConfig.o: /usr/include/openssl/dh.h /usr/include/openssl/sha.h
WorkspaceConfig.o: /usr/include/openssl/x509_vfy.h
WorkspaceConfig.o: /usr/include/openssl/lhash.h /usr/include/openssl/pkcs7.h
WorkspaceConfig.o: /usr/include/openssl/pem2.h /usr/include/openssl/err.h
WorkspaceConfig.o: /usr/include/errno.h /usr/include/bits/errno.h
WorkspaceConfig.o: /usr/include/linux/errno.h /usr/include/asm/errno.h
WorkspaceConfig.o: /usr/include/asm-generic/errno.h
WorkspaceConfig.o: /usr/include/asm-generic/errno-base.h
WorkspaceConfig.o: /usr/include/openssl/conf.h WorkspaceConfig.h
WorkspaceConfig.o: ServiceConfig.h Shock.h parse_url.h UserAgent.h Base64.h
WorkspaceConfig.o: ServiceDispatcher.h JSONRPC.h WorkspaceDB.h
WorkspaceConfig.o: WorkspaceTypes.h DispatchContext.h Logging.h
WorkspaceDB.o: WorkspaceDB.h WorkspaceTypes.h DispatchContext.h AuthToken.h
WorkspaceDB.o: Logging.h PathParser.h WorkspaceService.h WorkspaceErrors.h
WorkspaceDB.o: WorkspaceState.h SigningCerts.h /usr/include/openssl/bio.h
WorkspaceDB.o: /usr/include/openssl/e_os2.h
WorkspaceDB.o: /usr/include/openssl/opensslconf.h
WorkspaceDB.o: /usr/include/openssl/opensslconf-x86_64.h /usr/include/stdio.h
WorkspaceDB.o: /usr/include/features.h /usr/include/sys/cdefs.h
WorkspaceDB.o: /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h
WorkspaceDB.o: /usr/include/gnu/stubs-64.h /usr/include/bits/types.h
WorkspaceDB.o: /usr/include/bits/typesizes.h /usr/include/libio.h
WorkspaceDB.o: /usr/include/_G_config.h /usr/include/wchar.h
WorkspaceDB.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
WorkspaceDB.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
WorkspaceDB.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
WorkspaceDB.o: /usr/include/endian.h /usr/include/bits/endian.h
WorkspaceDB.o: /usr/include/bits/byteswap.h /usr/include/sys/types.h
WorkspaceDB.o: /usr/include/time.h /usr/include/sys/select.h
WorkspaceDB.o: /usr/include/bits/select.h /usr/include/bits/sigset.h
WorkspaceDB.o: /usr/include/bits/time.h /usr/include/sys/sysmacros.h
WorkspaceDB.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
WorkspaceDB.o: /usr/include/openssl/stack.h /usr/include/openssl/safestack.h
WorkspaceDB.o: /usr/include/openssl/opensslv.h
WorkspaceDB.o: /usr/include/openssl/ossl_typ.h
WorkspaceDB.o: /usr/include/openssl/symhacks.h /usr/include/openssl/pem.h
WorkspaceDB.o: /usr/include/openssl/evp.h /usr/include/openssl/fips.h
WorkspaceDB.o: /usr/include/openssl/objects.h /usr/include/openssl/obj_mac.h
WorkspaceDB.o: /usr/include/openssl/asn1.h /usr/include/openssl/bn.h
WorkspaceDB.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
WorkspaceDB.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
WorkspaceDB.o: /usr/include/bits/posix2_lim.h /usr/include/openssl/x509.h
WorkspaceDB.o: /usr/include/openssl/buffer.h /usr/include/openssl/ec.h
WorkspaceDB.o: /usr/include/openssl/ecdsa.h /usr/include/openssl/ecdh.h
WorkspaceDB.o: /usr/include/openssl/rsa.h /usr/include/openssl/dsa.h
WorkspaceDB.o: /usr/include/openssl/dh.h /usr/include/openssl/sha.h
WorkspaceDB.o: /usr/include/openssl/x509_vfy.h /usr/include/openssl/lhash.h
WorkspaceDB.o: /usr/include/openssl/pkcs7.h /usr/include/openssl/pem2.h
WorkspaceDB.o: /usr/include/openssl/err.h /usr/include/errno.h
WorkspaceDB.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
WorkspaceDB.o: /usr/include/asm/errno.h /usr/include/asm-generic/errno.h
WorkspaceDB.o: /usr/include/asm-generic/errno-base.h
WorkspaceDB.o: /usr/include/openssl/conf.h WorkspaceConfig.h ServiceConfig.h
WorkspaceDB.o: Shock.h parse_url.h UserAgent.h Base64.h ServiceDispatcher.h
WorkspaceDB.o: JSONRPC.h
WorkspaceService.o: WorkspaceService.h WorkspaceErrors.h WorkspaceState.h
WorkspaceService.o: AuthToken.h SigningCerts.h /usr/include/openssl/bio.h
WorkspaceService.o: /usr/include/openssl/e_os2.h
WorkspaceService.o: /usr/include/openssl/opensslconf.h
WorkspaceService.o: /usr/include/openssl/opensslconf-x86_64.h
WorkspaceService.o: /usr/include/stdio.h /usr/include/features.h
WorkspaceService.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
WorkspaceService.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h
WorkspaceService.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
WorkspaceService.o: /usr/include/libio.h /usr/include/_G_config.h
WorkspaceService.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
WorkspaceService.o: /usr/include/bits/sys_errlist.h
WorkspaceService.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
WorkspaceService.o: /usr/include/bits/waitflags.h
WorkspaceService.o: /usr/include/bits/waitstatus.h /usr/include/endian.h
WorkspaceService.o: /usr/include/bits/endian.h /usr/include/bits/byteswap.h
WorkspaceService.o: /usr/include/sys/types.h /usr/include/time.h
WorkspaceService.o: /usr/include/sys/select.h /usr/include/bits/select.h
WorkspaceService.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
WorkspaceService.o: /usr/include/sys/sysmacros.h
WorkspaceService.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
WorkspaceService.o: /usr/include/openssl/stack.h
WorkspaceService.o: /usr/include/openssl/safestack.h
WorkspaceService.o: /usr/include/openssl/opensslv.h
WorkspaceService.o: /usr/include/openssl/ossl_typ.h
WorkspaceService.o: /usr/include/openssl/symhacks.h
WorkspaceService.o: /usr/include/openssl/pem.h /usr/include/openssl/evp.h
WorkspaceService.o: /usr/include/openssl/fips.h
WorkspaceService.o: /usr/include/openssl/objects.h
WorkspaceService.o: /usr/include/openssl/obj_mac.h
WorkspaceService.o: /usr/include/openssl/asn1.h /usr/include/openssl/bn.h
WorkspaceService.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
WorkspaceService.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
WorkspaceService.o: /usr/include/bits/posix2_lim.h
WorkspaceService.o: /usr/include/openssl/x509.h /usr/include/openssl/buffer.h
WorkspaceService.o: /usr/include/openssl/ec.h /usr/include/openssl/ecdsa.h
WorkspaceService.o: /usr/include/openssl/ecdh.h /usr/include/openssl/rsa.h
WorkspaceService.o: /usr/include/openssl/dsa.h /usr/include/openssl/dh.h
WorkspaceService.o: /usr/include/openssl/sha.h
WorkspaceService.o: /usr/include/openssl/x509_vfy.h
WorkspaceService.o: /usr/include/openssl/lhash.h /usr/include/openssl/pkcs7.h
WorkspaceService.o: /usr/include/openssl/pem2.h /usr/include/openssl/err.h
WorkspaceService.o: /usr/include/errno.h /usr/include/bits/errno.h
WorkspaceService.o: /usr/include/linux/errno.h /usr/include/asm/errno.h
WorkspaceService.o: /usr/include/asm-generic/errno.h
WorkspaceService.o: /usr/include/asm-generic/errno-base.h
WorkspaceService.o: /usr/include/openssl/conf.h WorkspaceConfig.h
WorkspaceService.o: ServiceConfig.h Shock.h parse_url.h UserAgent.h Base64.h
WorkspaceService.o: ServiceDispatcher.h JSONRPC.h WorkspaceDB.h
WorkspaceService.o: WorkspaceTypes.h DispatchContext.h Logging.h
y.o: UserAgent.h parse_url.h RootCertificates.h /usr/include/openssl/x509v3.h
y.o: /usr/include/openssl/bio.h /usr/include/openssl/e_os2.h
y.o: /usr/include/openssl/opensslconf.h
y.o: /usr/include/openssl/opensslconf-x86_64.h /usr/include/stdio.h
y.o: /usr/include/features.h /usr/include/sys/cdefs.h
y.o: /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h
y.o: /usr/include/gnu/stubs-64.h /usr/include/bits/types.h
y.o: /usr/include/bits/typesizes.h /usr/include/libio.h
y.o: /usr/include/_G_config.h /usr/include/wchar.h
y.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
y.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
y.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
y.o: /usr/include/endian.h /usr/include/bits/endian.h
y.o: /usr/include/bits/byteswap.h /usr/include/sys/types.h
y.o: /usr/include/time.h /usr/include/sys/select.h /usr/include/bits/select.h
y.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
y.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
y.o: /usr/include/alloca.h /usr/include/openssl/stack.h
y.o: /usr/include/openssl/safestack.h /usr/include/openssl/opensslv.h
y.o: /usr/include/openssl/ossl_typ.h /usr/include/openssl/symhacks.h
y.o: /usr/include/openssl/x509.h /usr/include/openssl/buffer.h
y.o: /usr/include/openssl/evp.h /usr/include/openssl/fips.h
y.o: /usr/include/openssl/objects.h /usr/include/openssl/obj_mac.h
y.o: /usr/include/openssl/asn1.h /usr/include/openssl/bn.h
y.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
y.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
y.o: /usr/include/bits/posix2_lim.h /usr/include/openssl/ec.h
y.o: /usr/include/openssl/ecdsa.h /usr/include/openssl/ecdh.h
y.o: /usr/include/openssl/rsa.h /usr/include/openssl/dsa.h
y.o: /usr/include/openssl/dh.h /usr/include/openssl/sha.h
y.o: /usr/include/openssl/x509_vfy.h /usr/include/openssl/lhash.h
y.o: /usr/include/openssl/pkcs7.h /usr/include/openssl/conf.h
tests/path_parser.o: PathParser.h WorkspaceService.h WorkspaceErrors.h
tests/path_parser.o: WorkspaceState.h AuthToken.h SigningCerts.h
tests/path_parser.o: /usr/include/openssl/bio.h /usr/include/openssl/e_os2.h
tests/path_parser.o: /usr/include/openssl/opensslconf.h
tests/path_parser.o: /usr/include/openssl/opensslconf-x86_64.h
tests/path_parser.o: /usr/include/stdio.h /usr/include/features.h
tests/path_parser.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
tests/path_parser.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h
tests/path_parser.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
tests/path_parser.o: /usr/include/libio.h /usr/include/_G_config.h
tests/path_parser.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
tests/path_parser.o: /usr/include/bits/sys_errlist.h
tests/path_parser.o: /usr/include/openssl/crypto.h /usr/include/stdlib.h
tests/path_parser.o: /usr/include/bits/waitflags.h
tests/path_parser.o: /usr/include/bits/waitstatus.h /usr/include/endian.h
tests/path_parser.o: /usr/include/bits/endian.h /usr/include/bits/byteswap.h
tests/path_parser.o: /usr/include/sys/types.h /usr/include/time.h
tests/path_parser.o: /usr/include/sys/select.h /usr/include/bits/select.h
tests/path_parser.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
tests/path_parser.o: /usr/include/sys/sysmacros.h
tests/path_parser.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
tests/path_parser.o: /usr/include/openssl/stack.h
tests/path_parser.o: /usr/include/openssl/safestack.h
tests/path_parser.o: /usr/include/openssl/opensslv.h
tests/path_parser.o: /usr/include/openssl/ossl_typ.h
tests/path_parser.o: /usr/include/openssl/symhacks.h
tests/path_parser.o: /usr/include/openssl/pem.h /usr/include/openssl/evp.h
tests/path_parser.o: /usr/include/openssl/fips.h
tests/path_parser.o: /usr/include/openssl/objects.h
tests/path_parser.o: /usr/include/openssl/obj_mac.h
tests/path_parser.o: /usr/include/openssl/asn1.h /usr/include/openssl/bn.h
tests/path_parser.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
tests/path_parser.o: /usr/include/bits/local_lim.h
tests/path_parser.o: /usr/include/linux/limits.h
tests/path_parser.o: /usr/include/bits/posix2_lim.h
tests/path_parser.o: /usr/include/openssl/x509.h
tests/path_parser.o: /usr/include/openssl/buffer.h /usr/include/openssl/ec.h
tests/path_parser.o: /usr/include/openssl/ecdsa.h /usr/include/openssl/ecdh.h
tests/path_parser.o: /usr/include/openssl/rsa.h /usr/include/openssl/dsa.h
tests/path_parser.o: /usr/include/openssl/dh.h /usr/include/openssl/sha.h
tests/path_parser.o: /usr/include/openssl/x509_vfy.h
tests/path_parser.o: /usr/include/openssl/lhash.h
tests/path_parser.o: /usr/include/openssl/pkcs7.h /usr/include/openssl/pem2.h
tests/path_parser.o: /usr/include/openssl/err.h /usr/include/errno.h
tests/path_parser.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
tests/path_parser.o: /usr/include/asm/errno.h
tests/path_parser.o: /usr/include/asm-generic/errno.h
tests/path_parser.o: /usr/include/asm-generic/errno-base.h
tests/path_parser.o: /usr/include/openssl/conf.h WorkspaceConfig.h
tests/path_parser.o: ServiceConfig.h Shock.h parse_url.h UserAgent.h Base64.h
tests/path_parser.o: ServiceDispatcher.h JSONRPC.h WorkspaceDB.h
tests/path_parser.o: WorkspaceTypes.h DispatchContext.h Logging.h
