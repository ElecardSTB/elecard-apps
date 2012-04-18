
LIBSUFFIX = .a
DLLSUFFIX = .so

OUT = $(BUILD_TARGET)libedlna
SRC = CdsObjects/CdsDidlSerializer.c\
CdsObjects/CdsErrors.c\
CdsObjects/CdsMediaClass.c\
CdsObjects/CdsObject.c\
HttpFiles/DlnaHttp.c\
HttpFiles/DlnaHttpClient.c\
HttpFiles/DlnaHttpServer.c\
ILibParsers.c\
ILibAsyncSocket.c\
ILibAsyncUDPSocket.c\
ILibAsyncServerSocket.c\
ILibWebClient.c\
ILibWebServer.c\
ILibThreadPool.c\
DMR_MicroStack.c\
MediaServerCP_ControlPoint.c\
ILibSSDPClient.c\
MediaServerBrowser/FilteringBrowser.c\
MediaServerBrowser/MediaServerControlPoint.c\
FileAbstractionFiles/FileIoAbstraction.c\
ProtocolInfoParser/DLNAProtocolInfo.c\
StringUtils/MimeTypes.c\
StringUtils/UTF8Utils.c\
PlaylistTrackManager/BitArray.c\
PlaylistTrackManager/CircularBuffer.c\
PlaylistTrackManager/IndexBlocks.c\
PlaylistTrackManager/PlayListManager.c\
PlaylistTrackManager/PlaylistManagerPC.c\
PlaylistTrackManager/PlaylistManagerS.c\
MediaRenderer/DMR.c\
PlaySingleUri/PlaySingleUri.c
HDR = CdsObjects/CdsDidlSerializer.h\
CdsObjects/CdsErrors.h\
CdsObjects/CdsMediaClass.h\
CdsObjects/CdsObject.h\
CdsObjects/CdsStrings.h\
HttpFiles/DlnaHttp.h\
HttpFiles/DlnaHttpClient.h\
HttpFiles/DlnaHttpServer.h\
ILibParsers.h\
ILibAsyncSocket.h\
ILibAsyncUDPSocket.h\
ILibAsyncServerSocket.h\
ILibWebClient.h\
ILibWebServer.h\
ILibThreadPool.h\
DMR_MicroStack.h\
MediaServerCP_ControlPoint.h\
ILibSSDPClient.h\
UPnPControlPointStructs.h\
MediaServerBrowser/FilteringBrowser.h\
MediaServerBrowser/MediaServerControlPoint.h\
FileAbstractionFiles/FileIoAbstraction.h\
ProtocolInfoParser/DLNAProtocolInfo.h\
StringUtils/MimeTypes.h\
StringUtils/UTF8Utils.h\
PlaylistTrackManager/BitArray.h\
PlaylistTrackManager/CircularBuffer.h\
PlaylistTrackManager/IndexBlocks.h\
PlaylistTrackManager/PlayListManager.h\
PlaylistTrackManager/PlaylistManagerPC.h\
PlaylistTrackManager/PlaylistManagerS.h\
MediaRenderer/DMR.h\
MediaRenderer/DMRCommon.h\
MediaRenderer/DMRConfiguration.h\
PlaySingleUri/PlaySingleUri.h

OBJ = $(patsubst %.c,$(BUILD_TARGET)%.o,$(SRC))
MYCFLAGS = -I./ -I../  -I./CdsObjects  -I./HttpFiles  -I./MediaServerBrowser  -I./FileAbstractionFiles  -I./ProtocolInfoParser  -I./StringUtils \
	-I./PlaylistTrackManager  -I./MediaRenderer  -I./PlaySingleUri \
	-DINCLUDE_FEATURE_PLAYSINGLEURI -DINCLUDE_FEATURE_DISPLAY -DINCLUDE_FEATURE_VOLUME -DMICROSTACK_NO_STDAFX -DMSCP \
	-Os -Wall -Wextra -D_POSIX -D_DEBUG -D_VERBOSE -DOutputDebugString=puts -D_FILE_OFFSET_BITS=64 \

ifneq ($(BUILD_TARGET),)
    CC=${CROSS_COMPILE}gcc
    AR=${CROSS_COMPILE}ar
    LD=${CROSS_COMPILE}gcc
endif

$(BUILD_TARGET)%.o:%.c $(HDR)
	@test -z $(BUILD_TARGET) || mkdir -p `dirname $@`
	$(CC) -c $(MYCFLAGS) -fPIC -o $@ $<

all: $(OUT)$(DLLSUFFIX) $(OUT)$(LIBSUFFIX)

$(OUT)$(DLLSUFFIX): $(OBJ)
	if [ "$(BUILD_TARGET)" -a ! -d "$(BUILD_TARGET)" ]; then mkdir -p $(BUILD_TARGET); fi
	$(LD) -shared -o $@ $(OBJ) -lm

$(OUT)$(LIBSUFFIX): $(OBJ)
	if [ "$(BUILD_TARGET)" -a ! -d "$(BUILD_TARGET)" ]; then mkdir -p $(BUILD_TARGET); fi
	$(AR) rcs $@ $(OBJ)

clean:
	rm -f $(OBJ) $(OUT)$(LIBSUFFIX) $(OUT)$(DLLSUFFIX)
	if [ "$(BUILD_TARGET)" ]; then rm -rf $(BUILD_TARGET); fi

