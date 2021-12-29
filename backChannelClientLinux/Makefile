#
# backChannelClient makefile
#

include $(DEVDIR)/backChannel/MakefileCommon.inc

INCS=-I$(TOOLDIR)/genCppTools 
INCS+=-I$(TOOLDIR)/CommonCppDartCode 
INCS+=-I$(TOOLDIR)/CommonCppDartCode/Messages 
INCS+=-I$(EXTTOOLDIR)/json
INCS+=-I$(WORKDIR)

CXXFLAGS+=$(INCS) -std=c++17
LDFLAGS=-L$(LIBDIR)

BACKCHANNELCLIENTAPP=backChannelClientApp
BACKCHANNELCLIENTAPP_SRC:=backChannelClientApp.cpp hyperCubeClient.cpp
BACKCHANNELCLIENTAPP_EXE=$(BINDIR)/$(BACKCHANNELCLIENTAPP)

COBJS:=$(BACKCHANNELCLIENTAPP_SRC:.cpp=.o)

%.o : %.cpp *.h 
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(LIBDIRGENLIB): $(TOOLDIR)/genCppTools/*.cpp $(TOOLDIR)/genCppTools/*.h
	make -C tools

$(BACKCHANNELCLIENTAPP_EXE): $(COBJS) $(LIBDIRGENLIB)
	$(CXX) -o $(BACKCHANNELCLIENTAPP_EXE) $(COBJS) $(LDFLAGS) -l$(GENLIB)

all: $(BACKCHANNELCLIENTAPP_EXE)

clean:
	-$(RM) $(BACKCHANNELCLIENTAPP_EXE) $(COBJS)
