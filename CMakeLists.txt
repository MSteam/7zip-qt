# ============================================================================
# 7-Zip -> Qt/Linux port : milestone C.0
# CMake build of the portable 7-Zip engine as a STATIC library (no Qt).
#
# This file was derived FAITHFULLY from the working GNU-make build:
#   CPP/7zip/Bundles/Alone2/makefile.gcc
#     -> includes CPP/7zip/Bundles/Format7zF/Arc_gcc.mak  (engine object groups)
#     -> includes CPP/7zip/7zip_gcc.mak                   (per-object source rules,
#                                                          flags, defines)
#     -> includes CPP/7zip/LzmaDec_gcc.mak
#
# The library "sevenzip_engine" contains exactly the 311 engine sources that the
# working Linux `7zz` build links (322 objects in CPP/7zip/Bundles/Alone2/_o/)
# MINUS the 11 console front-end sources (CPP/7zip/UI/Console/*).
#
# DEVIATION from the task brief - WorkDir.cpp and ZipRegistry.cpp are NOT added:
#   The Windows GUI build (CPP/7zip/UI/GUI/makefile - an nmake/Win32 makefile
#   that produces 7zG.exe) lists WorkDir.cpp and ZipRegistry.cpp, but on Linux
#   they form an inseparable, non-portable pair that cannot be built in C.0:
#     * ZipRegistry.cpp includes Windows/Registry.h and calls the Win32 registry
#       API (HKEY, RegQueryValueEx, HKEY_CURRENT_USER, ...). Its backing source
#       CPP/Windows/Registry.cpp is intentionally excluded from every Linux/gcc
#       build and does not compile here - there is no Linux registry impl in the
#       tree. (Verified: 50+ compile errors on g++ 16.)
#     * WorkDir.cpp compiles, but its only caller-facing entry point loads
#       settings via NWorkDir::CInfo::Load(), which is DEFINED ONLY inside
#       ZipRegistry.cpp (line 507). So WorkDir.cpp pulls in ZipRegistry.cpp as a
#       hard link dependency (verified: "undefined reference to
#       NWorkDir::CInfo::Load()" when WorkDir.cpp is linked without it).
#   Adding either would require writing a NEW Linux settings backend that
#   provides NWorkDir::CInfo::Load/Save - new, out-of-scope code that C.0 must
#   not introduce (no engine modifications, no stubs). They are deferred to the
#   Qt port (C.1+), which will supply its own settings backend in place of the
#   Win32 registry. This is the documented blocker for the requested additions.
#
# The "7zz" executable re-adds the 11 console sources, proving the library is
# complete and keeping the CLI buildable under CMake.
#
# DO NOT add -Werror: GCC 16 emits new warnings that break the stock sources.
# DO NOT add -I flags: every engine source uses #include "..." paths relative to
# its OWN directory (e.g. "../../../../C/7zCrc.h"), which the compiler resolves
# from the including file's location - exactly as the makefile does (its
# CXX_INCLUDE_FLAGS is empty). No _WIN32 -> MyWindows.h provides the COM shim.
# USE_ASM is NOT set in the reference build, so the C fallbacks (AesOpt.c,
# Sha256Opt.c, ...) are compiled as plain C; no .asm is used.
# ST_MODE is NOT set -> multi-threaded build (no -DZ7_ST); MT sources are included.
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(sevenzip_engine LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Compile flags, matching the working makefile.gcc build exactly ---------
# Reference flags: -O2 -DNDEBUG -D_REENTRANT -D_FILE_OFFSET_BITS=64
#                  -D_LARGEFILE_SOURCE -fPIC   (no -Werror, no -std, no -I)
set(SEVENZIP_DEFINES
  NDEBUG
  _REENTRANT
  _FILE_OFFSET_BITS=64
  _LARGEFILE_SOURCE
  # C.1a: share the same COM refcount ABI between the engine lib and the Qt app.
  # Required once COM objects cross threads (C.1b); set now for consistency so
  # every TU that touches a COM object agrees on the refcount implementation.
  Z7_COM_USE_ATOMIC
)

# Engine sources (C engine + CPP/Common + portable CPP/Windows subset +
# CPP/7zip/Common + all Archive handlers + all Compress codecs + all Crypto +
# CPP/7zip/UI/Common driver layer). 311 files.
set(SEVENZIP_ENGINE_SOURCES
    C/7zBuf2.c
    C/7zCrc.c
    C/7zCrcOpt.c
    C/7zStream.c
    C/Aes.c
    C/AesOpt.c
    C/Alloc.c
    C/Bcj2.c
    C/Bcj2Enc.c
    C/Blake2s.c
    C/Bra.c
    C/Bra86.c
    C/BraIA64.c
    C/BwtSort.c
    C/CpuArch.c
    C/Delta.c
    C/HuffEnc.c
    C/LzFind.c
    C/LzFindMt.c
    C/LzFindOpt.c
    C/Lzma2Dec.c
    C/Lzma2DecMt.c
    C/Lzma2Enc.c
    C/LzmaDec.c
    C/LzmaEnc.c
    C/Md5.c
    C/MtCoder.c
    C/MtDec.c
    C/Ppmd7.c
    C/Ppmd7Dec.c
    C/Ppmd7Enc.c
    C/Ppmd7aDec.c
    C/Ppmd8.c
    C/Ppmd8Dec.c
    C/Ppmd8Enc.c
    C/Sha1.c
    C/Sha1Opt.c
    C/Sha256.c
    C/Sha256Opt.c
    C/Sha3.c
    C/Sha512.c
    C/Sha512Opt.c
    C/Sort.c
    C/SwapBytes.c
    C/Threads.c
    C/Xxh64.c
    C/Xz.c
    C/XzCrc64.c
    C/XzCrc64Opt.c
    C/XzDec.c
    C/XzEnc.c
    C/XzIn.c
    C/ZstdDec.c
    CPP/7zip/Archive/7z/7zCompressionMode.cpp
    CPP/7zip/Archive/7z/7zDecode.cpp
    CPP/7zip/Archive/7z/7zEncode.cpp
    CPP/7zip/Archive/7z/7zExtract.cpp
    CPP/7zip/Archive/7z/7zFolderInStream.cpp
    CPP/7zip/Archive/7z/7zHandler.cpp
    CPP/7zip/Archive/7z/7zHandlerOut.cpp
    CPP/7zip/Archive/7z/7zHeader.cpp
    CPP/7zip/Archive/7z/7zIn.cpp
    CPP/7zip/Archive/7z/7zOut.cpp
    CPP/7zip/Archive/7z/7zProperties.cpp
    CPP/7zip/Archive/7z/7zRegister.cpp
    CPP/7zip/Archive/7z/7zSpecStream.cpp
    CPP/7zip/Archive/7z/7zUpdate.cpp
    CPP/7zip/Archive/ApfsHandler.cpp
    CPP/7zip/Archive/ApmHandler.cpp
    CPP/7zip/Archive/ArHandler.cpp
    CPP/7zip/Archive/ArjHandler.cpp
    CPP/7zip/Archive/Base64Handler.cpp
    CPP/7zip/Archive/Bz2Handler.cpp
    CPP/7zip/Archive/Cab/CabBlockInStream.cpp
    CPP/7zip/Archive/Cab/CabHandler.cpp
    CPP/7zip/Archive/Cab/CabHeader.cpp
    CPP/7zip/Archive/Cab/CabIn.cpp
    CPP/7zip/Archive/Cab/CabRegister.cpp
    CPP/7zip/Archive/Chm/ChmHandler.cpp
    CPP/7zip/Archive/Chm/ChmIn.cpp
    CPP/7zip/Archive/ComHandler.cpp
    CPP/7zip/Archive/Common/CoderMixer2.cpp
    CPP/7zip/Archive/Common/DummyOutStream.cpp
    CPP/7zip/Archive/Common/FindSignature.cpp
    CPP/7zip/Archive/Common/HandlerOut.cpp
    CPP/7zip/Archive/Common/InStreamWithCRC.cpp
    CPP/7zip/Archive/Common/ItemNameUtils.cpp
    CPP/7zip/Archive/Common/MultiStream.cpp
    CPP/7zip/Archive/Common/OutStreamWithCRC.cpp
    CPP/7zip/Archive/Common/OutStreamWithSha1.cpp
    CPP/7zip/Archive/Common/ParseProperties.cpp
    CPP/7zip/Archive/CpioHandler.cpp
    CPP/7zip/Archive/CramfsHandler.cpp
    CPP/7zip/Archive/DeflateProps.cpp
    CPP/7zip/Archive/DmgHandler.cpp
    CPP/7zip/Archive/ElfHandler.cpp
    CPP/7zip/Archive/ExtHandler.cpp
    CPP/7zip/Archive/FatHandler.cpp
    CPP/7zip/Archive/FlvHandler.cpp
    CPP/7zip/Archive/GptHandler.cpp
    CPP/7zip/Archive/GzHandler.cpp
    CPP/7zip/Archive/HandlerCont.cpp
    CPP/7zip/Archive/HfsHandler.cpp
    CPP/7zip/Archive/IhexHandler.cpp
    CPP/7zip/Archive/Iso/IsoHandler.cpp
    CPP/7zip/Archive/Iso/IsoHeader.cpp
    CPP/7zip/Archive/Iso/IsoIn.cpp
    CPP/7zip/Archive/Iso/IsoRegister.cpp
    CPP/7zip/Archive/LpHandler.cpp
    CPP/7zip/Archive/LzhHandler.cpp
    CPP/7zip/Archive/LzmaHandler.cpp
    CPP/7zip/Archive/MachoHandler.cpp
    CPP/7zip/Archive/MbrHandler.cpp
    CPP/7zip/Archive/MslzHandler.cpp
    CPP/7zip/Archive/MubHandler.cpp
    CPP/7zip/Archive/Nsis/NsisDecode.cpp
    CPP/7zip/Archive/Nsis/NsisHandler.cpp
    CPP/7zip/Archive/Nsis/NsisIn.cpp
    CPP/7zip/Archive/Nsis/NsisRegister.cpp
    CPP/7zip/Archive/NtfsHandler.cpp
    CPP/7zip/Archive/PeHandler.cpp
    CPP/7zip/Archive/PpmdHandler.cpp
    CPP/7zip/Archive/QcowHandler.cpp
    CPP/7zip/Archive/Rar/Rar5Handler.cpp
    CPP/7zip/Archive/Rar/RarHandler.cpp
    CPP/7zip/Archive/RpmHandler.cpp
    CPP/7zip/Archive/SparseHandler.cpp
    CPP/7zip/Archive/SplitHandler.cpp
    CPP/7zip/Archive/SquashfsHandler.cpp
    CPP/7zip/Archive/SwfHandler.cpp
    CPP/7zip/Archive/Tar/TarHandler.cpp
    CPP/7zip/Archive/Tar/TarHandlerOut.cpp
    CPP/7zip/Archive/Tar/TarHeader.cpp
    CPP/7zip/Archive/Tar/TarIn.cpp
    CPP/7zip/Archive/Tar/TarOut.cpp
    CPP/7zip/Archive/Tar/TarRegister.cpp
    CPP/7zip/Archive/Tar/TarUpdate.cpp
    CPP/7zip/Archive/Udf/UdfHandler.cpp
    CPP/7zip/Archive/Udf/UdfIn.cpp
    CPP/7zip/Archive/UefiHandler.cpp
    CPP/7zip/Archive/VdiHandler.cpp
    CPP/7zip/Archive/VhdHandler.cpp
    CPP/7zip/Archive/VhdxHandler.cpp
    CPP/7zip/Archive/VmdkHandler.cpp
    CPP/7zip/Archive/Wim/WimHandler.cpp
    CPP/7zip/Archive/Wim/WimHandlerOut.cpp
    CPP/7zip/Archive/Wim/WimIn.cpp
    CPP/7zip/Archive/Wim/WimRegister.cpp
    CPP/7zip/Archive/XarHandler.cpp
    CPP/7zip/Archive/XzHandler.cpp
    CPP/7zip/Archive/ZHandler.cpp
    CPP/7zip/Archive/Zip/ZipAddCommon.cpp
    CPP/7zip/Archive/Zip/ZipHandler.cpp
    CPP/7zip/Archive/Zip/ZipHandlerOut.cpp
    CPP/7zip/Archive/Zip/ZipIn.cpp
    CPP/7zip/Archive/Zip/ZipItem.cpp
    CPP/7zip/Archive/Zip/ZipOut.cpp
    CPP/7zip/Archive/Zip/ZipRegister.cpp
    CPP/7zip/Archive/Zip/ZipUpdate.cpp
    CPP/7zip/Archive/ZstdHandler.cpp
    CPP/7zip/Common/CWrappers.cpp
    CPP/7zip/Common/CreateCoder.cpp
    CPP/7zip/Common/FilePathAutoRename.cpp
    CPP/7zip/Common/FileStreams.cpp
    CPP/7zip/Common/FilterCoder.cpp
    CPP/7zip/Common/InBuffer.cpp
    CPP/7zip/Common/InOutTempBuffer.cpp
    CPP/7zip/Common/LimitedStreams.cpp
    CPP/7zip/Common/LockedStream.cpp
    CPP/7zip/Common/MemBlocks.cpp
    CPP/7zip/Common/MethodId.cpp
    CPP/7zip/Common/MethodProps.cpp
    CPP/7zip/Common/MultiOutStream.cpp
    CPP/7zip/Common/OffsetStream.cpp
    CPP/7zip/Common/OutBuffer.cpp
    CPP/7zip/Common/OutMemStream.cpp
    CPP/7zip/Common/ProgressMt.cpp
    CPP/7zip/Common/ProgressUtils.cpp
    CPP/7zip/Common/PropId.cpp
    CPP/7zip/Common/StreamBinder.cpp
    CPP/7zip/Common/StreamObjects.cpp
    CPP/7zip/Common/StreamUtils.cpp
    CPP/7zip/Common/UniqBlocks.cpp
    CPP/7zip/Common/VirtThread.cpp
    CPP/7zip/Compress/BZip2Crc.cpp
    CPP/7zip/Compress/BZip2Decoder.cpp
    CPP/7zip/Compress/BZip2Encoder.cpp
    CPP/7zip/Compress/BZip2Register.cpp
    CPP/7zip/Compress/Bcj2Coder.cpp
    CPP/7zip/Compress/Bcj2Register.cpp
    CPP/7zip/Compress/BcjCoder.cpp
    CPP/7zip/Compress/BcjRegister.cpp
    CPP/7zip/Compress/BitlDecoder.cpp
    CPP/7zip/Compress/BranchMisc.cpp
    CPP/7zip/Compress/BranchRegister.cpp
    CPP/7zip/Compress/ByteSwap.cpp
    CPP/7zip/Compress/CopyCoder.cpp
    CPP/7zip/Compress/CopyRegister.cpp
    CPP/7zip/Compress/Deflate64Register.cpp
    CPP/7zip/Compress/DeflateDecoder.cpp
    CPP/7zip/Compress/DeflateEncoder.cpp
    CPP/7zip/Compress/DeflateRegister.cpp
    CPP/7zip/Compress/DeltaFilter.cpp
    CPP/7zip/Compress/ImplodeDecoder.cpp
    CPP/7zip/Compress/LzOutWindow.cpp
    CPP/7zip/Compress/LzfseDecoder.cpp
    CPP/7zip/Compress/LzhDecoder.cpp
    CPP/7zip/Compress/Lzma2Decoder.cpp
    CPP/7zip/Compress/Lzma2Encoder.cpp
    CPP/7zip/Compress/Lzma2Register.cpp
    CPP/7zip/Compress/LzmaDecoder.cpp
    CPP/7zip/Compress/LzmaEncoder.cpp
    CPP/7zip/Compress/LzmaRegister.cpp
    CPP/7zip/Compress/LzmsDecoder.cpp
    CPP/7zip/Compress/LzxDecoder.cpp
    CPP/7zip/Compress/PpmdDecoder.cpp
    CPP/7zip/Compress/PpmdEncoder.cpp
    CPP/7zip/Compress/PpmdRegister.cpp
    CPP/7zip/Compress/PpmdZip.cpp
    CPP/7zip/Compress/QuantumDecoder.cpp
    CPP/7zip/Compress/Rar1Decoder.cpp
    CPP/7zip/Compress/Rar2Decoder.cpp
    CPP/7zip/Compress/Rar3Decoder.cpp
    CPP/7zip/Compress/Rar3Vm.cpp
    CPP/7zip/Compress/Rar5Decoder.cpp
    CPP/7zip/Compress/RarCodecsRegister.cpp
    CPP/7zip/Compress/ShrinkDecoder.cpp
    CPP/7zip/Compress/XpressDecoder.cpp
    CPP/7zip/Compress/XzDecoder.cpp
    CPP/7zip/Compress/XzEncoder.cpp
    CPP/7zip/Compress/ZDecoder.cpp
    CPP/7zip/Compress/ZlibDecoder.cpp
    CPP/7zip/Compress/ZlibEncoder.cpp
    CPP/7zip/Compress/ZstdDecoder.cpp
    CPP/7zip/Crypto/7zAes.cpp
    CPP/7zip/Crypto/7zAesRegister.cpp
    CPP/7zip/Crypto/HmacSha1.cpp
    CPP/7zip/Crypto/HmacSha256.cpp
    CPP/7zip/Crypto/MyAes.cpp
    CPP/7zip/Crypto/MyAesReg.cpp
    CPP/7zip/Crypto/Pbkdf2HmacSha1.cpp
    CPP/7zip/Crypto/RandGen.cpp
    CPP/7zip/Crypto/Rar20Crypto.cpp
    CPP/7zip/Crypto/Rar5Aes.cpp
    CPP/7zip/Crypto/RarAes.cpp
    CPP/7zip/Crypto/WzAes.cpp
    CPP/7zip/Crypto/ZipCrypto.cpp
    CPP/7zip/Crypto/ZipStrong.cpp
    CPP/7zip/UI/Common/ArchiveCommandLine.cpp
    CPP/7zip/UI/Common/ArchiveExtractCallback.cpp
    CPP/7zip/UI/Common/ArchiveOpenCallback.cpp
    CPP/7zip/UI/Common/Bench.cpp
    CPP/7zip/UI/Common/DefaultName.cpp
    CPP/7zip/UI/Common/EnumDirItems.cpp
    CPP/7zip/UI/Common/Extract.cpp
    CPP/7zip/UI/Common/ExtractingFilePath.cpp
    CPP/7zip/UI/Common/HashCalc.cpp
    CPP/7zip/UI/Common/LoadCodecs.cpp
    CPP/7zip/UI/Common/OpenArchive.cpp
    CPP/7zip/UI/Common/PropIDUtils.cpp
    CPP/7zip/UI/Common/SetProperties.cpp
    CPP/7zip/UI/Common/SortUtils.cpp
    CPP/7zip/UI/Common/TempFiles.cpp
    CPP/7zip/UI/Common/Update.cpp
    CPP/7zip/UI/Common/UpdateAction.cpp
    CPP/7zip/UI/Common/UpdateCallback.cpp
    CPP/7zip/UI/Common/UpdatePair.cpp
    CPP/7zip/UI/Common/UpdateProduce.cpp
    CPP/Common/CRC.cpp
    CPP/Common/CommandLineParser.cpp
    CPP/Common/CrcReg.cpp
    CPP/Common/DynLimBuf.cpp
    CPP/Common/IntToString.cpp
    CPP/Common/ListFileUtils.cpp
    CPP/Common/LzFindPrepare.cpp
    CPP/Common/Md5Reg.cpp
    CPP/Common/MyMap.cpp
    CPP/Common/MyString.cpp
    CPP/Common/MyVector.cpp
    CPP/Common/MyWindows.cpp
    CPP/Common/MyXml.cpp
    CPP/Common/NewHandler.cpp
    CPP/Common/Sha1Prepare.cpp
    CPP/Common/Sha1Reg.cpp
    CPP/Common/Sha256Prepare.cpp
    CPP/Common/Sha256Reg.cpp
    CPP/Common/Sha3Reg.cpp
    CPP/Common/Sha512Prepare.cpp
    CPP/Common/Sha512Reg.cpp
    CPP/Common/StdInStream.cpp
    CPP/Common/StdOutStream.cpp
    CPP/Common/StringConvert.cpp
    CPP/Common/StringToInt.cpp
    CPP/Common/UTFConvert.cpp
    CPP/Common/Wildcard.cpp
    CPP/Common/Xxh64Reg.cpp
    CPP/Common/XzCrc64Init.cpp
    CPP/Common/XzCrc64Reg.cpp
    CPP/Windows/ErrorMsg.cpp
    CPP/Windows/FileDir.cpp
    CPP/Windows/FileFind.cpp
    CPP/Windows/FileIO.cpp
    CPP/Windows/FileLink.cpp
    CPP/Windows/FileName.cpp
    CPP/Windows/PropVariant.cpp
    CPP/Windows/PropVariantConv.cpp
    CPP/Windows/PropVariantUtils.cpp
    CPP/Windows/Synchronization.cpp
    CPP/Windows/System.cpp
    CPP/Windows/SystemInfo.cpp
    CPP/Windows/TimeUtils.cpp
)

add_library(sevenzip_engine STATIC ${SEVENZIP_ENGINE_SOURCES})

target_compile_definitions(sevenzip_engine PRIVATE ${SEVENZIP_DEFINES})

# -O2 is implied by Release, but pin it so the build matches the reference
# regardless of CMAKE_BUILD_TYPE.
target_compile_options(sevenzip_engine PRIVATE -O2 -fPIC -Wall -Wextra)
set_target_properties(sevenzip_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The engine links pthread + dl (LIB2 = -lpthread -ldl in the makefile).
# Expose them to anything that links the library (the Qt app will need them too).
find_package(Threads REQUIRED)
target_link_libraries(sevenzip_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- 7zz console executable : engine + the 11 console front-end sources ------
set(SEVENZIP_CONSOLE_SOURCES
    CPP/7zip/UI/Console/BenchCon.cpp
    CPP/7zip/UI/Console/ConsoleClose.cpp
    CPP/7zip/UI/Console/ExtractCallbackConsole.cpp
    CPP/7zip/UI/Console/HashCon.cpp
    CPP/7zip/UI/Console/List.cpp
    CPP/7zip/UI/Console/Main.cpp
    CPP/7zip/UI/Console/MainAr.cpp
    CPP/7zip/UI/Console/OpenCallbackConsole.cpp
    CPP/7zip/UI/Console/PercentPrinter.cpp
    CPP/7zip/UI/Console/UpdateCallbackConsole.cpp
    CPP/7zip/UI/Console/UserInputUtils.cpp
)

add_executable(7zz ${SEVENZIP_CONSOLE_SOURCES})
target_compile_definitions(7zz PRIVATE ${SEVENZIP_DEFINES})
target_compile_options(7zz PRIVATE -O2 -fPIC -Wall -Wextra)

# Main.cpp selects the "z" program variant (CONSOLE_VARIANT_FLAGS in
# makefile.gcc: -DZ7_PROG_VARIANT_Z) which produces the "7-Zip (z)" banner.
set_source_files_properties(CPP/7zip/UI/Console/Main.cpp
  PROPERTIES COMPILE_DEFINITIONS "Z7_PROG_VARIANT_Z")

# IMPORTANT: link the WHOLE archive.
# 7-Zip registers every codec / archive handler / crypto method through the
# side effects of GLOBAL static constructors in the *Register.cpp objects
# (e.g. LzmaRegister, 7zRegister, Bcj2Register, MyAesReg). Those objects expose
# no symbol that the front-end references directly, so a normal static-library
# link drops them and the engine reports "E_NOTIMPL" at run time.
# The reference makefile.gcc never hits this because it links all .o files
# directly into the executable. To reproduce that behaviour while keeping the
# engine as a clean static library, we force the whole archive in. The Qt app
# (C.1+) must link the engine the same way.
target_link_libraries(7zz PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>")


# ============================================================================
# C.1a : Qt/Linux GUI subtree (threaded modal-progress core, isolated demo).
# Guarded so 7zz stays buildable even without Qt: only added when BUILD_QT_GUI
# is ON (default) AND find_package(Qt6 ... QUIET) actually finds Qt6 Widgets.
# ============================================================================
option(BUILD_QT_GUI "Build the Qt/Linux GUI subtree (CPP/7zip/UI/Qt)" ON)

if(BUILD_QT_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND AND TARGET Qt6::Widgets)
    message(STATUS "Qt6 found - building Qt GUI subtree (CPP/7zip/UI/Qt)")
    add_subdirectory(CPP/7zip/UI/Qt)
  else()
    message(STATUS "Qt6 (Widgets) not found - skipping Qt GUI subtree; 7zz still builds")
  endif()
endif()
