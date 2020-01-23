CXX=x86_64-w64-mingw32-g++
OPENCV=opencv-3.4.7/build/install
CPPFLAGS=-I. -I$(OPENCV)/include
CXXFLAGS=-g3 -Wall -O0 $(shell pkg-config opencv --cflags)
LDFLAGS=-static-libgcc -static-libstdc++ -L$(OPENCV)/lib -L$(OPENCV)/share/OpenCV/3rdparty/lib/
LIBS=-lopencv_calib3d347 -lopencv_core347 -lopencv_dnn347 -lopencv_features2d347 -lopencv_flann347 -lopencv_highgui347 -lopencv_imgcodecs347 -lopencv_imgproc347 -lopencv_ml347 -lopencv_objdetect347 -lopencv_photo347 -lopencv_shape347 -lopencv_stitching347 -lopencv_superres347 -lopencv_video347 -lopencv_videoio347 -lopencv_videostab347
#-llibpng -lzlib -llibjpeg-turbo -llibwebp -llibjasper -lIlmImf -lquirc -llibprotobuf -llibtiff -Wl,--end-group

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
