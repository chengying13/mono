thisdir = class/System.Xml.Linq
SUBDIRS = 
include ../../build/rules.make

LIBRARY = System.Xml.Linq.dll
LIB_REFS = System System.Core System.Xml
LIB_MCS_FLAGS =

NO_SERIALIZATION_PROFILE := $(filter basic build, $(PROFILE))
ifndef NO_SERIALIZATION_PROFILE
LIB_REFS += System.Runtime.Serialization
LIB_MCS_FLAGS += -d:FEATURE_SERIALIZATION
endif

ifeq (2.1, $(FRAMEWORK_VERSION))
LIB_MCS_FLAGS += -d:MONO_HYBRID_SYSTEM_XML
endif

TEST_LIB_REFS = System.Core System.Xml System
TEST_MCS_FLAGS =
EXTRA_DISTFILES =

include ../../build/library.make
