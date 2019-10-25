CXX?=g++

CPPFLAGS=-I.
CXXFLAGS=-g3 -Wall -O3 $(shell pkg-config opencv --cflags)
LIBS=$(shell pkg-config opencv --libs)

SRCS=top.cc analysis.cc

OBJS=$(patsubst %.cc,$(BUILD)/%.o,$(SRCS))
PPIS=$(patsubst %.cc,$(BUILD)/%.i,$(SRCS))
DEPS=$(patsubst %.o,%.d,$(OBJS))

BUILD=build

EXE=micetracker

.PHONY: all
all: $(EXE)

$(OBJS):$(BUILD)/%.o:%.cc
	@mkdir -p `dirname $@`
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -o $@ -c $<

$(PPIS):$(BUILD)/%.i:%.cc
	@mkdir -p `dirname $@`
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -o $@ -E $<

$(EXE): $(OBJS)
	@mkdir -p `dirname $@`
	$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

.PHONY: expand
expand: $(PPIS)

.PHONY: clean
clean:
	rm -Rf $(BUILD)

-include $(DEPS)
