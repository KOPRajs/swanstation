#include "file_system.h"
#include "byte_stream.h"
#include "log.h"
#include "string_util.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/param.h>
#else
#include <malloc.h>
#endif

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
#include <shlobj.h>
#else
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#endif

Log_SetChannel(FileSystem);

namespace FileSystem {

#ifdef __ANDROID__

static JavaVM* s_android_jvm;
static jobject s_android_FileHelper_object;
static jclass s_android_FileHelper_class;
static jmethodID s_android_FileHelper_openURIAsFileDescriptor;
static jmethodID s_android_FileHelper_FindFiles;
static jmethodID s_android_FileHelper_StatFile;
static jclass s_android_FileHelper_FindResult_class;
static jfieldID s_android_FileHelper_FindResult_name;
static jfieldID s_android_FileHelper_FindResult_relativeName;
static jfieldID s_android_FileHelper_FindResult_size;
static jfieldID s_android_FileHelper_FindResult_modifiedTime;
static jfieldID s_android_FileHelper_FindResult_flags;
static jclass s_android_FileHelper_StatResult_class;
static jfieldID s_android_FileHelper_StatResult_size;
static jfieldID s_android_FileHelper_StatResult_modifiedTime;
static jfieldID s_android_FileHelper_StatResult_flags;
static jmethodID s_android_FileHelper_getDisplayName;
static jmethodID s_android_FileHelper_getRelativePathForURIPath;

// helper for retrieving the current per-thread jni environment
static JNIEnv* GetJNIEnv()
{
  JNIEnv* env;
  if (s_android_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
    return nullptr;
  else
    return env;
}

static bool IsUriPath(const std::string_view& path)
{
  return StringUtil::StartsWith(path, "content:/") || StringUtil::StartsWith(path, "file:/");
}

static bool UriHelpersAreAvailable()
{
  return (s_android_FileHelper_object != nullptr);
}

void SetAndroidFileHelper(void* jvm, void* env, void* object)
{
  if (s_android_FileHelper_object)
  {
    JNIEnv* jenv = GetJNIEnv();
    jenv->DeleteGlobalRef(s_android_FileHelper_FindResult_class);
    s_android_FileHelper_FindResult_name = {};
    s_android_FileHelper_FindResult_relativeName = {};
    s_android_FileHelper_FindResult_size = {};
    s_android_FileHelper_FindResult_modifiedTime = {};
    s_android_FileHelper_FindResult_flags = {};
    s_android_FileHelper_FindResult_class = {};
    jenv->DeleteGlobalRef(s_android_FileHelper_StatResult_class);
    s_android_FileHelper_StatResult_size = {};
    s_android_FileHelper_StatResult_modifiedTime = {};
    s_android_FileHelper_StatResult_flags = {};
    s_android_FileHelper_StatResult_class = {};

    jenv->DeleteGlobalRef(s_android_FileHelper_object);
    jenv->DeleteGlobalRef(s_android_FileHelper_class);
    s_android_FileHelper_getRelativePathForURIPath = {};
    s_android_FileHelper_getDisplayName = {};
    s_android_FileHelper_openURIAsFileDescriptor = {};
    s_android_FileHelper_StatFile = {};
    s_android_FileHelper_FindFiles = {};
    s_android_FileHelper_object = {};
    s_android_FileHelper_class = {};
    s_android_jvm = {};
  }

  if (!object)
    return;

  JNIEnv* jenv = static_cast<JNIEnv*>(env);
  s_android_jvm = static_cast<JavaVM*>(jvm);
  s_android_FileHelper_object = jenv->NewGlobalRef(static_cast<jobject>(object));
  jclass fh_class = jenv->GetObjectClass(static_cast<jobject>(object));
  s_android_FileHelper_class = static_cast<jclass>(jenv->NewGlobalRef(fh_class));
  jenv->DeleteLocalRef(fh_class);

  s_android_FileHelper_openURIAsFileDescriptor =
    jenv->GetMethodID(s_android_FileHelper_class, "openURIAsFileDescriptor", "(Ljava/lang/String;Ljava/lang/String;)I");
  s_android_FileHelper_StatFile =
    jenv->GetMethodID(s_android_FileHelper_class, "statFile",
                      "(Ljava/lang/String;)Lcom/github/stenzek/duckstation/FileHelper$StatResult;");
  s_android_FileHelper_FindFiles =
    jenv->GetMethodID(s_android_FileHelper_class, "findFiles",
                      "(Ljava/lang/String;I)[Lcom/github/stenzek/duckstation/FileHelper$FindResult;");
  s_android_FileHelper_getDisplayName =
    jenv->GetMethodID(s_android_FileHelper_class, "getDisplayNameForURIPath", "(Ljava/lang/String;)Ljava/lang/String;");
  s_android_FileHelper_getRelativePathForURIPath =
    jenv->GetMethodID(s_android_FileHelper_class, "getRelativePathForURIPath",
                      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
  jclass fr_class = jenv->FindClass("com/github/stenzek/duckstation/FileHelper$FindResult");
  s_android_FileHelper_FindResult_class = static_cast<jclass>(jenv->NewGlobalRef(fr_class));
  jenv->DeleteLocalRef(fr_class);

  s_android_FileHelper_FindResult_relativeName =
    jenv->GetFieldID(s_android_FileHelper_FindResult_class, "relativeName", "Ljava/lang/String;");
  s_android_FileHelper_FindResult_name =
    jenv->GetFieldID(s_android_FileHelper_FindResult_class, "name", "Ljava/lang/String;");
  s_android_FileHelper_FindResult_size = jenv->GetFieldID(s_android_FileHelper_FindResult_class, "size", "J");
  s_android_FileHelper_FindResult_modifiedTime =
    jenv->GetFieldID(s_android_FileHelper_FindResult_class, "modifiedTime", "J");
  s_android_FileHelper_FindResult_flags = jenv->GetFieldID(s_android_FileHelper_FindResult_class, "flags", "I");

  jclass st_class = jenv->FindClass("com/github/stenzek/duckstation/FileHelper$StatResult");
  s_android_FileHelper_StatResult_class = static_cast<jclass>(jenv->NewGlobalRef(st_class));
  jenv->DeleteLocalRef(st_class);

  s_android_FileHelper_StatResult_size = jenv->GetFieldID(s_android_FileHelper_StatResult_class, "size", "J");
  s_android_FileHelper_StatResult_modifiedTime =
    jenv->GetFieldID(s_android_FileHelper_StatResult_class, "modifiedTime", "J");
  s_android_FileHelper_StatResult_flags = jenv->GetFieldID(s_android_FileHelper_StatResult_class, "flags", "I");
}

static std::FILE* OpenUriFile(const char* path, const char* mode)
{
  // translate C modes to Java modes
  TinyString mode_trimmed;
  std::size_t mode_len = std::strlen(mode);
  for (size_t i = 0; i < mode_len; i++)
  {
    if (mode[i] == 'r' || mode[i] == 'w' || mode[i] == '+')
      mode_trimmed.AppendCharacter(mode[i]);
  }

  // TODO: Handle append mode by seeking to end.
  const char* java_mode = nullptr;
  if (mode_trimmed == "r")
    java_mode = "r";
  else if (mode_trimmed == "r+")
    java_mode = "rw";
  else if (mode_trimmed == "w")
    java_mode = "w";
  else if (mode_trimmed == "w+")
    java_mode = "rwt";

  if (!java_mode)
  {
    Log_ErrorPrintf("Could not translate file mode '%s' ('%s')", mode, mode_trimmed.GetCharArray());
    return nullptr;
  }

  // Hand off to Java...
  JNIEnv* env = GetJNIEnv();
  jstring path_jstr = env->NewStringUTF(path);
  jstring mode_jstr = env->NewStringUTF(java_mode);
  int fd =
    env->CallIntMethod(s_android_FileHelper_object, s_android_FileHelper_openURIAsFileDescriptor, path_jstr, mode_jstr);
  env->DeleteLocalRef(mode_jstr);
  env->DeleteLocalRef(path_jstr);

  // Just in case...
  if (env->ExceptionCheck())
  {
    env->ExceptionClear();
    return nullptr;
  }

  if (fd < 0)
    return nullptr;

  // Convert to a C file object.
  std::FILE* fp = fdopen(fd, mode);
  if (!fp)
  {
    Log_ErrorPrintf("Failed to convert FD %d to C FILE for '%s'.", fd, path);
    close(fd);
    return nullptr;
  }

  return fp;
}

static bool FindUriFiles(const char* path, const char* pattern, u32 flags, FindResultsArray* pVector)
{
  if (!s_android_FileHelper_object)
    return false;

  JNIEnv* env = GetJNIEnv();

  jstring path_jstr = env->NewStringUTF(path);
  jobjectArray arr = static_cast<jobjectArray>(env->CallObjectMethod(
    s_android_FileHelper_object, s_android_FileHelper_FindFiles, path_jstr, static_cast<int>(flags)));
  env->DeleteLocalRef(path_jstr);
  if (!arr)
    return false;

  // small speed optimization for '*' case
  bool hasWildCards = false;
  bool wildCardMatchAll = false;
  if (std::strpbrk(pattern, "*?") != nullptr)
  {
    hasWildCards = true;
    wildCardMatchAll = !(std::strcmp(pattern, "*"));
  }

  jsize count = env->GetArrayLength(arr);
  for (jsize i = 0; i < count; i++)
  {
    jobject result = env->GetObjectArrayElement(arr, i);
    if (!result)
      continue;

    jstring result_name_obj = static_cast<jstring>(env->GetObjectField(result, s_android_FileHelper_FindResult_name));
    jstring result_relative_name_obj =
      static_cast<jstring>(env->GetObjectField(result, s_android_FileHelper_FindResult_relativeName));
    const u64 result_size = static_cast<u64>(env->GetLongField(result, s_android_FileHelper_FindResult_size));
    const u64 result_modified_time =
      static_cast<u64>(env->GetLongField(result, s_android_FileHelper_FindResult_modifiedTime));
    const u32 result_flags = static_cast<u32>(env->GetIntField(result, s_android_FileHelper_FindResult_flags));

    if (result_name_obj && result_relative_name_obj)
    {
      const char* result_name = env->GetStringUTFChars(result_name_obj, nullptr);
      const char* result_relative_name = env->GetStringUTFChars(result_relative_name_obj, nullptr);
      if (result_relative_name)
      {
        // match the filename
        bool matched;
        if (hasWildCards)
        {
          matched = wildCardMatchAll || StringUtil::WildcardMatch(result_relative_name, pattern);
        }
        else
        {
          matched = std::strcmp(result_relative_name, pattern) == 0;
        }

        if (matched)
        {
          FILESYSTEM_FIND_DATA ffd;
          ffd.FileName = ((flags & FILESYSTEM_FIND_RELATIVE_PATHS) != 0) ? result_relative_name : result_name;
          ffd.Attributes = result_flags;
          ffd.ModificationTime.SetUnixTimestamp(result_modified_time);
          ffd.Size = result_size;
          pVector->push_back(std::move(ffd));
        }
      }

      if (result_name)
        env->ReleaseStringUTFChars(result_name_obj, result_name);
      if (result_relative_name)
        env->ReleaseStringUTFChars(result_relative_name_obj, result_relative_name);
    }

    if (result_name_obj)
      env->DeleteLocalRef(result_name_obj);
    if (result_relative_name_obj)
      env->DeleteLocalRef(result_relative_name_obj);

    env->DeleteLocalRef(result);
  }

  env->DeleteLocalRef(arr);
  return true;
}

static bool StatUriFile(const char* path, FILESYSTEM_STAT_DATA* sd)
{
  if (!s_android_FileHelper_object)
    return false;

  JNIEnv* env = GetJNIEnv();

  jstring path_jstr = env->NewStringUTF(path);
  jobject result = static_cast<jobjectArray>(
    env->CallObjectMethod(s_android_FileHelper_object, s_android_FileHelper_StatFile, path_jstr));
  env->DeleteLocalRef(path_jstr);
  if (!result)
    return false;

  const u64 result_size = static_cast<u64>(env->GetLongField(result, s_android_FileHelper_StatResult_size));
  const u64 result_modified_time =
    static_cast<u64>(env->GetLongField(result, s_android_FileHelper_StatResult_modifiedTime));
  const u32 result_flags = static_cast<u32>(env->GetIntField(result, s_android_FileHelper_StatResult_flags));
  sd->Attributes = result_flags;
  sd->ModificationTime.SetUnixTimestamp(result_modified_time);
  sd->Size = result_size;

  env->DeleteLocalRef(result);
  return true;
}

static bool GetDisplayNameForUriPath(const char* path, std::string* result)
{
  if (!s_android_FileHelper_object)
    return false;

  JNIEnv* env = GetJNIEnv();

  jstring path_jstr = env->NewStringUTF(path);
  jstring result_jstr = static_cast<jstring>(
    env->CallObjectMethod(s_android_FileHelper_object, s_android_FileHelper_getDisplayName, path_jstr));
  env->DeleteLocalRef(path_jstr);
  if (!result_jstr)
    return false;

  const char* result_name = env->GetStringUTFChars(result_jstr, nullptr);
  if (result_name)
    result->assign(result_name);
  else
    result->clear();

  env->ReleaseStringUTFChars(result_jstr, result_name);
  env->DeleteLocalRef(result_jstr);
  return true;
}

static bool GetRelativePathForUriPath(const char* path, const char* filename, std::string* result)
{
  if (!s_android_FileHelper_object)
    return false;

  JNIEnv* env = GetJNIEnv();

  jstring path_jstr = env->NewStringUTF(path);
  jstring filename_jstr = env->NewStringUTF(filename);
  jstring result_jstr = static_cast<jstring>(env->CallObjectMethod(
    s_android_FileHelper_object, s_android_FileHelper_getRelativePathForURIPath, path_jstr, filename_jstr));
  env->DeleteLocalRef(filename_jstr);
  env->DeleteLocalRef(path_jstr);
  if (!result_jstr)
    return false;

  const char* result_name = env->GetStringUTFChars(result_jstr, nullptr);
  if (result_name)
    result->assign(result_name);
  else
    result->clear();

  env->ReleaseStringUTFChars(result_jstr, result_name);
  env->DeleteLocalRef(result_jstr);
  return true;
}

#endif // __ANDROID__
void CanonicalizePath(char* Destination, u32 cbDestination, const char* Path, bool OSPath /*= true*/)
{
  u32 i, j;
  // get length
  u32 pathLength = static_cast<u32>(std::strlen(Path));

  // clone to a local buffer if the same pointer
  if (Destination == Path)
  {
    char* pathClone = (char*)alloca(pathLength + 1);
    StringUtil::Strlcpy(pathClone, Path, pathLength + 1);
    Path = pathClone;
  }

  // zero destination
  std::memset(Destination, 0, cbDestination);

  // iterate path
  u32 destinationLength = 0;
  for (i = 0; i < pathLength;)
  {
    char prevCh = (i > 0) ? Path[i - 1] : '\0';
    char currentCh = Path[i];
    char nextCh = (i < (pathLength - 1)) ? Path[i + 1] : '\0';

    if (currentCh == '.')
    {
      if (prevCh == '\\' || prevCh == '/' || prevCh == '\0')
      {
        // handle '.'
        if (nextCh == '\\' || nextCh == '/' || nextCh == '\0')
        {
          // skip '.\'
          i++;

          // remove the previous \, if we have one trailing the dot it'll append it anyway
          if (destinationLength > 0)
            Destination[--destinationLength] = '\0';
          // if there was no previous \, skip past the next one
          else if (nextCh != '\0')
            i++;

          continue;
        }
        // handle '..'
        else if (nextCh == '.')
        {
          char afterNext = ((i + 1) < pathLength) ? Path[i + 2] : '\0';
          if (afterNext == '\\' || afterNext == '/' || afterNext == '\0')
          {
            // remove one directory of the path, including the /.
            if (destinationLength > 1)
            {
              for (j = destinationLength - 2; j > 0; j--)
              {
                if (Destination[j] == '\\' || Destination[j] == '/')
                  break;
              }

              destinationLength = j;
#ifdef _DEBUG
              Destination[destinationLength] = '\0';
#endif
            }

            // skip the dot segment
            i += 2;
            continue;
          }
        }
      }
    }

    // fix ospath
    if (OSPath && (currentCh == '\\' || currentCh == '/'))
      currentCh = FS_OSPATH_SEPARATOR_CHARACTER;

    // copy character
    if (destinationLength < cbDestination)
    {
      Destination[destinationLength++] = currentCh;
#ifdef _DEBUG
      Destination[destinationLength] = '\0';
#endif
    }
    else
      break;

    // increment position by one
    i++;
  }

  // if we end up with the empty string, return '.'
  if (destinationLength == 0)
    Destination[destinationLength++] = '.';

  // ensure nullptr termination
  if (destinationLength < cbDestination)
    Destination[destinationLength] = '\0';
  else
    Destination[destinationLength - 1] = '\0';
}

void CanonicalizePath(String& Destination, const char* Path, bool OSPath /* = true */)
{
  // the function won't actually write any more characters than are present to the buffer,
  // so we can get away with simply passing both pointers if they are the same.
  if (Destination.GetWriteableCharArray() != Path)
  {
    // otherwise, resize the destination to at least the source's size, and then pass as-is
    Destination.Reserve(static_cast<u32>(std::strlen(Path)) + 1);
  }

  CanonicalizePath(Destination.GetWriteableCharArray(), Destination.GetBufferSize(), Path, OSPath);
  Destination.UpdateSize();
}

void CanonicalizePath(String& Destination, bool OSPath /* = true */)
{
  CanonicalizePath(Destination, Destination);
}

void CanonicalizePath(std::string& path, bool OSPath /*= true*/)
{
  CanonicalizePath(path.data(), static_cast<u32>(path.size() + 1), path.c_str(), OSPath);
}

static inline bool FileSystemCharacterIsSane(char c, bool StripSlashes)
{
  if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != ' ' && c != '_' &&
      c != '-' && c != '.')
  {
    if (!StripSlashes && (c == '/' || c == '\\'))
      return true;

    return false;
  }

  return true;
}

void SanitizeFileName(char* Destination, u32 cbDestination, const char* FileName, bool StripSlashes /* = true */)
{
  u32 i;
  u32 fileNameLength = static_cast<u32>(std::strlen(FileName));

  if (FileName == Destination)
  {
    for (i = 0; i < fileNameLength; i++)
    {
      if (!FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = '_';
    }
  }
  else
  {
    for (i = 0; i < fileNameLength && i < cbDestination; i++)
    {
      if (FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = FileName[i];
      else
        Destination[i] = '_';
    }
  }
}

void SanitizeFileName(String& Destination, const char* FileName, bool StripSlashes /* = true */)
{
  u32 i;
  u32 fileNameLength;

  // if same buffer, use fastpath
  if (Destination.GetWriteableCharArray() == FileName)
  {
    fileNameLength = Destination.GetLength();
    for (i = 0; i < fileNameLength; i++)
    {
      if (!FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = '_';
    }
  }
  else
  {
    fileNameLength = static_cast<u32>(std::strlen(FileName));
    Destination.Resize(fileNameLength);
    for (i = 0; i < fileNameLength; i++)
    {
      if (FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = FileName[i];
      else
        Destination[i] = '_';
    }
  }
}

void SanitizeFileName(String& Destination, bool StripSlashes /* = true */)
{
  return SanitizeFileName(Destination, Destination, StripSlashes);
}

void SanitizeFileName(std::string& Destination, bool StripSlashes /* = true*/)
{
  const std::size_t len = Destination.length();
  for (std::size_t i = 0; i < len; i++)
  {
    if (!FileSystemCharacterIsSane(Destination[i], StripSlashes))
      Destination[i] = '_';
  }
}

bool IsAbsolutePath(const std::string_view& path)
{
#ifdef _WIN32
  return (path.length() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
          path[1] == ':' && (path[2] == '/' || path[2] == '\\'));
#else
  return (path.length() >= 1 && path[0] == '/');
#endif
}

std::string_view StripExtension(const std::string_view& path)
{
  std::string_view::size_type pos = path.rfind('.');
  if (pos == std::string::npos)
    return path;

  return path.substr(0, pos);
}

std::string ReplaceExtension(const std::string_view& path, const std::string_view& new_extension)
{
#ifdef __ANDROID__
  // This is more complex on android because the path may not contain the actual filename.
  if (IsUriPath(path))
  {
    std::string display_name(GetDisplayNameFromPath(path));
    std::string_view::size_type pos = display_name.rfind('.');
    if (pos == std::string::npos)
      return std::string(path);

    display_name.erase(pos + 1);
    display_name.append(new_extension);

    return BuildRelativePath(path, display_name);
  }
#endif

  std::string_view::size_type pos = path.rfind('.');
  if (pos == std::string::npos)
    return std::string(path);

  std::string ret(path, 0, pos + 1);
  ret.append(new_extension);
  return ret;
}

static std::string_view::size_type GetLastSeperatorPosition(const std::string_view& filename, bool include_separator)
{
  std::string_view::size_type last_separator = filename.rfind('/');
  if (include_separator && last_separator != std::string_view::npos)
    last_separator++;

#if defined(_WIN32)
  std::string_view::size_type other_last_separator = filename.rfind('\\');
  if (other_last_separator != std::string_view::npos)
  {
    if (include_separator)
      other_last_separator++;
    if (last_separator == std::string_view::npos || other_last_separator > last_separator)
      last_separator = other_last_separator;
  }

#elif defined(__ANDROID__)
  if (IsUriPath(filename))
  {
    // scoped storage rubbish
    std::string_view::size_type other_last_separator = filename.rfind("%2F");
    if (other_last_separator != std::string_view::npos)
    {
      if (include_separator)
        other_last_separator += 3;
      if (last_separator == std::string_view::npos || other_last_separator > last_separator)
        last_separator = other_last_separator;
    }
    std::string_view::size_type lower_other_last_separator = filename.rfind("%2f");
    if (lower_other_last_separator != std::string_view::npos)
    {
      if (include_separator)
        lower_other_last_separator += 3;
      if (last_separator == std::string_view::npos || lower_other_last_separator > last_separator)
        last_separator = lower_other_last_separator;
    }
  }
#endif

  return last_separator;
}

std::string GetDisplayNameFromPath(const std::string_view& path)
{
#if defined(__ANDROID__)
  std::string result;

  if (IsUriPath(path))
  {
    std::string temp(path);
    if (!GetDisplayNameForUriPath(temp.c_str(), &result))
      result = std::move(temp);
  }
  else
  {
    result = path;
  }

  return result;
#else
  return std::string(GetFileNameFromPath(path));
#endif
}

std::string_view GetPathDirectory(const std::string_view& path)
{
  std::string::size_type pos = GetLastSeperatorPosition(path, false);
  if (pos == std::string_view::npos)
    return {};

  return path.substr(0, pos);
}

std::string_view GetFileNameFromPath(const std::string_view& path)
{
  std::string_view::size_type pos = GetLastSeperatorPosition(path, true);
  if (pos == std::string_view::npos)
    return path;

  return path.substr(pos);
}

std::string_view GetFileTitleFromPath(const std::string_view& path)
{
  std::string_view filename(GetFileNameFromPath(path));
  std::string::size_type pos = filename.rfind('.');
  if (pos == std::string_view::npos)
    return filename;

  return filename.substr(0, pos);
}

std::string BuildRelativePath(const std::string_view& filename, const std::string_view& new_filename)
{
  std::string new_string;

#ifdef __ANDROID__
  if (IsUriPath(filename) &&
      GetRelativePathForUriPath(std::string(filename).c_str(), std::string(new_filename).c_str(), &new_string))
  {
    return new_string;
  }
#endif

  std::string_view::size_type pos = GetLastSeperatorPosition(filename, true);
  if (pos != std::string_view::npos)
    new_string.assign(filename, 0, pos);
  new_string.append(new_filename);
  return new_string;
}

std::unique_ptr<ByteStream> OpenFile(const char* FileName, u32 Flags)
{
  // has a path
  if (FileName[0] == '\0')
    return nullptr;

  // TODO: Handle Android content URIs here.

  // forward to local file wrapper
  return ByteStream_OpenFileStream(FileName, Flags);
}

FileSystem::ManagedCFilePtr OpenManagedCFile(const char* filename, const char* mode)
{
  return ManagedCFilePtr(OpenCFile(filename, mode), [](std::FILE* fp) { std::fclose(fp); });
}

std::FILE* OpenCFile(const char* filename, const char* mode)
{
#ifdef _WIN32
  int filename_len = static_cast<int>(std::strlen(filename));
  int mode_len = static_cast<int>(std::strlen(mode));
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filename, filename_len, nullptr, 0);
  int wmodelen = MultiByteToWideChar(CP_UTF8, 0, mode, mode_len, nullptr, 0);
  if (wlen > 0 && wmodelen > 0)
  {
    wchar_t* wfilename = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wlen + 1)));
    wchar_t* wmode = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wmodelen + 1)));
    wlen = MultiByteToWideChar(CP_UTF8, 0, filename, filename_len, wfilename, wlen);
    wmodelen = MultiByteToWideChar(CP_UTF8, 0, mode, mode_len, wmode, wmodelen);
    if (wlen > 0 && wmodelen > 0)
    {
      wfilename[wlen] = 0;
      wmode[wmodelen] = 0;

      std::FILE* fp;
      if (_wfopen_s(&fp, wfilename, wmode) != 0)
        return nullptr;

      return fp;
    }
  }

  std::FILE* fp;
  if (fopen_s(&fp, filename, mode) != 0)
    return nullptr;

  return fp;
#else
#ifdef __ANDROID__
  if (IsUriPath(filename) && UriHelpersAreAvailable())
    return OpenUriFile(filename, mode);
#endif

  return std::fopen(filename, mode);
#endif
}

int FSeek64(std::FILE* fp, s64 offset, int whence)
{
#ifdef _WIN32
  return _fseeki64(fp, offset, whence);
#else
  // Prevent truncation on platforms which don't have a 64-bit off_t (Android 32-bit).
  if constexpr (sizeof(off_t) != sizeof(s64))
  {
    if (offset < std::numeric_limits<off_t>::min() || offset > std::numeric_limits<off_t>::max())
      return -1;
  }

  return fseeko(fp, static_cast<off_t>(offset), whence);
#endif
}

s64 FTell64(std::FILE* fp)
{
#ifdef _WIN32
  return static_cast<s64>(_ftelli64(fp));
#else
  return static_cast<s64>(ftello(fp));
#endif
}

std::optional<std::vector<u8>> ReadBinaryFile(const char* filename)
{
  ManagedCFilePtr fp = OpenManagedCFile(filename, "rb");
  if (!fp)
    return std::nullopt;

  return ReadBinaryFile(fp.get());
}

std::optional<std::vector<u8>> ReadBinaryFile(std::FILE* fp)
{
  std::fseek(fp, 0, SEEK_END);
  long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size < 0)
    return std::nullopt;

  std::vector<u8> res(static_cast<size_t>(size));
  if (size > 0 && std::fread(res.data(), 1u, static_cast<size_t>(size), fp) != static_cast<size_t>(size))
    return std::nullopt;

  return res;
}

std::optional<std::string> ReadFileToString(const char* filename)
{
  ManagedCFilePtr fp = OpenManagedCFile(filename, "rb");
  if (!fp)
    return std::nullopt;

  return ReadFileToString(fp.get());
}

std::optional<std::string> ReadFileToString(std::FILE* fp)
{
  std::fseek(fp, 0, SEEK_END);
  long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size < 0)
    return std::nullopt;

  std::string res;
  res.resize(static_cast<size_t>(size));
  if (size > 0 && std::fread(res.data(), 1u, static_cast<size_t>(size), fp) != static_cast<size_t>(size))
    return std::nullopt;

  return res;
}

bool WriteBinaryFile(const char* filename, const void* data, size_t data_length)
{
  ManagedCFilePtr fp = OpenManagedCFile(filename, "wb");
  if (!fp)
    return false;

  if (data_length > 0 && std::fwrite(data, 1u, data_length, fp.get()) != data_length)
    return false;

  return true;
}

#ifdef _WIN32
static u32 TranslateWin32Attributes(u32 Win32Attributes)
{
  u32 r = 0;

  if (Win32Attributes & FILE_ATTRIBUTE_DIRECTORY)
    r |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;
  if (Win32Attributes & FILE_ATTRIBUTE_READONLY)
    r |= FILESYSTEM_FILE_ATTRIBUTE_READ_ONLY;
  if (Win32Attributes & FILE_ATTRIBUTE_COMPRESSED)
    r |= FILESYSTEM_FILE_ATTRIBUTE_COMPRESSED;

  return r;
}

static DWORD WrapGetFileAttributes(const wchar_t* path)
{
  return GetFileAttributesW(path);
}

static const u32 READ_DIRECTORY_CHANGES_NOTIFY_FILTER = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

static u32 RecursiveFindFiles(const char* OriginPath, const char* ParentPath, const char* Path, const char* Pattern,
                              u32 Flags, FileSystem::FindResultsArray* pResults)
{
  std::string tempStr;
  if (Path)
  {
    if (ParentPath)
      tempStr = StringUtil::StdStringFromFormat("%s\\%s\\%s\\*", OriginPath, ParentPath, Path);
    else
      tempStr = StringUtil::StdStringFromFormat("%s\\%s\\*", OriginPath, Path);
  }
  else
  {
    tempStr = StringUtil::StdStringFromFormat("%s\\*", OriginPath);
  }

  // holder for utf-8 conversion
  WIN32_FIND_DATAW wfd;
  std::string utf8_filename;
  utf8_filename.reserve(countof(wfd.cFileName) * 2);

  HANDLE hFind = FindFirstFileW(StringUtil::UTF8StringToWideString(tempStr).c_str(), &wfd);

  if (hFind == INVALID_HANDLE_VALUE)
    return 0;

  // small speed optimization for '*' case
  bool hasWildCards = false;
  bool wildCardMatchAll = false;
  u32 nFiles = 0;
  if (std::strpbrk(Pattern, "*?") != nullptr)
  {
    hasWildCards = true;
    wildCardMatchAll = !(std::strcmp(Pattern, "*"));
  }

  // iterate results
  do
  {
    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN && !(Flags & FILESYSTEM_FIND_HIDDEN_FILES))
      continue;

    if (wfd.cFileName[0] == L'.')
    {
      if (wfd.cFileName[1] == L'\0' || (wfd.cFileName[1] == L'.' && wfd.cFileName[2] == L'\0'))
        continue;
    }

    if (!StringUtil::WideStringToUTF8String(utf8_filename, wfd.cFileName))
      continue;

    FILESYSTEM_FIND_DATA outData;
    outData.Attributes = 0;

    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      if (Flags & FILESYSTEM_FIND_RECURSIVE)
      {
        // recurse into this directory
        if (ParentPath != nullptr)
        {
          const std::string recurseDir = StringUtil::StdStringFromFormat("%s\\%s", ParentPath, Path);
          nFiles += RecursiveFindFiles(OriginPath, recurseDir.c_str(), utf8_filename.c_str(), Pattern, Flags, pResults);
        }
        else
        {
          nFiles += RecursiveFindFiles(OriginPath, Path, utf8_filename.c_str(), Pattern, Flags, pResults);
        }
      }

      if (!(Flags & FILESYSTEM_FIND_FOLDERS))
        continue;

      outData.Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
      if (!(Flags & FILESYSTEM_FIND_FILES))
        continue;
    }

    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
      outData.Attributes |= FILESYSTEM_FILE_ATTRIBUTE_READ_ONLY;

    // match the filename
    if (hasWildCards)
    {
      if (!wildCardMatchAll && !StringUtil::WildcardMatch(utf8_filename.c_str(), Pattern))
        continue;
    }
    else
    {
      if (std::strcmp(utf8_filename.c_str(), Pattern) != 0)
        continue;
    }

    // add file to list
    // TODO string formatter, clean this mess..
    if (!(Flags & FILESYSTEM_FIND_RELATIVE_PATHS))
    {
      if (ParentPath != nullptr)
        outData.FileName =
          StringUtil::StdStringFromFormat("%s\\%s\\%s\\%s", OriginPath, ParentPath, Path, utf8_filename.c_str());
      else if (Path != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s\\%s", OriginPath, Path, utf8_filename.c_str());
      else
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s", OriginPath, utf8_filename.c_str());
    }
    else
    {
      if (ParentPath != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s\\%s", ParentPath, Path, utf8_filename.c_str());
      else if (Path != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s", Path, utf8_filename.c_str());
      else
        outData.FileName = utf8_filename;
    }

    outData.ModificationTime.SetWindowsFileTime(&wfd.ftLastWriteTime);
    outData.Size = (u64)wfd.nFileSizeHigh << 32 | (u64)wfd.nFileSizeLow;

    nFiles++;
    pResults->push_back(std::move(outData));
  } while (FindNextFileW(hFind, &wfd) == TRUE);
  FindClose(hFind);

  return nFiles;
}

bool FileSystem::FindFiles(const char* Path, const char* Pattern, u32 Flags, FindResultsArray* pResults)
{
  // has a path
  if (Path[0] == '\0')
    return false;

  // clear result array
  if (!(Flags & FILESYSTEM_FIND_KEEP_ARRAY))
    pResults->clear();

  // enter the recursive function
  return (RecursiveFindFiles(Path, nullptr, nullptr, Pattern, Flags, pResults) > 0);
}

bool FileSystem::StatFile(const char* path, FILESYSTEM_STAT_DATA* pStatData)
{
  // has a path
  if (path[0] == '\0')
    return false;

  // convert to wide string
  int len = static_cast<int>(std::strlen(path));
  int wlen = MultiByteToWideChar(CP_UTF8, 0, path, len, nullptr, 0);
  if (wlen <= 0)
    return false;

  wchar_t* wpath = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wlen + 1)));
  wlen = MultiByteToWideChar(CP_UTF8, 0, path, len, wpath, wlen);
  if (wlen <= 0)
    return false;

  wpath[wlen] = 0;

  // determine attributes for the path. if it's a directory, things have to be handled differently..
  DWORD fileAttributes = GetFileAttributesW(wpath);
  if (fileAttributes == INVALID_FILE_ATTRIBUTES)
    return false;

  // test if it is a directory
  HANDLE hFile;
  if (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)
  {
    hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  }
  else
  {
    hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
  }

  // createfile succeded?
  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  // use GetFileInformationByHandle
  BY_HANDLE_FILE_INFORMATION bhfi;
  if (GetFileInformationByHandle(hFile, &bhfi) == FALSE)
  {
    CloseHandle(hFile);
    return false;
  }

  // close handle
  CloseHandle(hFile);

  // fill in the stat data
  pStatData->Attributes = TranslateWin32Attributes(bhfi.dwFileAttributes);
  pStatData->ModificationTime.SetWindowsFileTime(&bhfi.ftLastWriteTime);
  pStatData->Size = ((u64)bhfi.nFileSizeHigh) << 32 | (u64)bhfi.nFileSizeLow;
  return true;
}

bool FileSystem::StatFile(std::FILE* fp, FILESYSTEM_STAT_DATA* pStatData)
{
  const int fd = _fileno(fp);
  if (fd < 0)
    return false;

  struct _stat64 st;
  if (_fstati64(fd, &st) != 0)
    return false;

  // parse attributes
  pStatData->Attributes = 0;
  if ((st.st_mode & _S_IFMT) == _S_IFDIR)
    pStatData->Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;

  // parse times
  pStatData->ModificationTime.SetUnixTimestamp((Timestamp::UnixTimestampValue)st.st_mtime);

  // parse size
  if ((st.st_mode & _S_IFMT) == _S_IFREG)
    pStatData->Size = static_cast<u64>(st.st_size);
  else
    pStatData->Size = 0;

  return true;
}

bool FileSystem::FileExists(const char* path)
{
  // has a path
  if (path[0] == '\0')
    return false;

  // convert to wide string
  int len = static_cast<int>(std::strlen(path));
  int wlen = MultiByteToWideChar(CP_UTF8, 0, path, len, nullptr, 0);
  if (wlen <= 0)
    return false;

  wchar_t* wpath = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wlen + 1)));
  wlen = MultiByteToWideChar(CP_UTF8, 0, path, len, wpath, wlen);
  if (wlen <= 0)
    return false;

  wpath[wlen] = 0;

  // determine attributes for the path. if it's a directory, things have to be handled differently..
  DWORD fileAttributes = WrapGetFileAttributes(wpath);
  if (fileAttributes == INVALID_FILE_ATTRIBUTES)
    return false;

  if (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    return false;
  else
    return true;
}

bool FileSystem::DirectoryExists(const char* path)
{
  // has a path
  if (path[0] == '\0')
    return false;

  // convert to wide string
  int len = static_cast<int>(std::strlen(path));
  int wlen = MultiByteToWideChar(CP_UTF8, 0, path, len, nullptr, 0);
  if (wlen <= 0)
    return false;

  wchar_t* wpath = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wlen + 1)));
  wlen = MultiByteToWideChar(CP_UTF8, 0, path, len, wpath, wlen);
  if (wlen <= 0)
    return false;

  wpath[wlen] = 0;

  // determine attributes for the path. if it's a directory, things have to be handled differently..
  DWORD fileAttributes = WrapGetFileAttributes(wpath);
  if (fileAttributes == INVALID_FILE_ATTRIBUTES)
    return false;

  if (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    return true;
  else
    return false;
}

bool FileSystem::CreateDirectory(const char* Path, bool Recursive)
{
  std::wstring wpath(StringUtil::UTF8StringToWideString(Path));

  // has a path
  if (wpath[0] == L'\0')
    return false;

    // try just flat-out, might work if there's no other segments that have to be made
  if (CreateDirectoryW(wpath.c_str(), nullptr))
    return true;

  // check error
  DWORD lastError = GetLastError();
  if (lastError == ERROR_ALREADY_EXISTS)
  {
    // check the attributes
    u32 Attributes = WrapGetFileAttributes(wpath.c_str());
    if (Attributes != INVALID_FILE_ATTRIBUTES && Attributes & FILE_ATTRIBUTE_DIRECTORY)
      return true;
    return false;
  }
  else if (lastError == ERROR_PATH_NOT_FOUND)
  {
    // part of the path does not exist, so we'll create the parent folders, then
    // the full path again. allocate another buffer with the same length
    u32 pathLength = static_cast<u32>(wpath.size());
    wchar_t* tempStr = (wchar_t*)alloca(sizeof(wchar_t) * (pathLength + 1));

    // create directories along the path
    for (u32 i = 0; i < pathLength; i++)
    {
      if (wpath[i] == L'\\' || wpath[i] == L'/')
      {
        tempStr[i] = L'\0';

        const BOOL result = CreateDirectoryW(tempStr, nullptr);

        if (!result)
        {
          lastError = GetLastError();
          if (lastError != ERROR_ALREADY_EXISTS) // fine, continue to next path segment
            return false;
        }
      }

      tempStr[i] = wpath[i];
    }

    // re-create the end if it's not a separator, check / as well because windows can interpret them
    if (wpath[pathLength - 1] != L'\\' && wpath[pathLength - 1] != L'/')
    {
      const BOOL result = CreateDirectoryW(wpath.c_str(), nullptr);

      if (!result)
      {
        lastError = GetLastError();
        if (lastError != ERROR_ALREADY_EXISTS)
          return false;
      }
    }

    // ok
    return true;
  }
  // unhandled error
  return false;
}

bool FileSystem::DeleteFile(const char* Path)
{
  if (Path[0] == '\0')
    return false;

  const std::wstring wpath(StringUtil::UTF8StringToWideString(Path));
  const DWORD fileAttributes = WrapGetFileAttributes(wpath.c_str());
  if (fileAttributes == INVALID_FILE_ATTRIBUTES || fileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    return false;

  return (DeleteFileW(wpath.c_str()) == TRUE);
}

bool FileSystem::RenamePath(const char* OldPath, const char* NewPath)
{
  const std::wstring old_wpath(StringUtil::UTF8StringToWideString(OldPath));
  const std::wstring new_wpath(StringUtil::UTF8StringToWideString(NewPath));

  if (!MoveFileExW(old_wpath.c_str(), new_wpath.c_str(), MOVEFILE_REPLACE_EXISTING))
  {
    Log_ErrorPrintf("MoveFileEx('%s', '%s') failed: %08X", OldPath, NewPath, GetLastError());
    return false;
  }
  return true;
}

#else
static u32 RecursiveFindFiles(const char* OriginPath, const char* ParentPath, const char* Path, const char* Pattern,
                              u32 Flags, FindResultsArray* pResults)
{
  std::string tempStr;
  if (Path)
  {
    if (ParentPath)
      tempStr = StringUtil::StdStringFromFormat("%s/%s/%s", OriginPath, ParentPath, Path);
    else
      tempStr = StringUtil::StdStringFromFormat("%s/%s", OriginPath, Path);
  }
  else
  {
    tempStr = StringUtil::StdStringFromFormat("%s", OriginPath);
  }

  DIR* pDir = opendir(tempStr.c_str());
  if (pDir == nullptr)
    return 0;

  // small speed optimization for '*' case
  bool hasWildCards = false;
  bool wildCardMatchAll = false;
  u32 nFiles = 0;
  if (std::strpbrk(Pattern, "*?"))
  {
    hasWildCards = true;
    wildCardMatchAll = (std::strcmp(Pattern, "*") == 0);
  }

  // iterate results
  PathString full_path;
  struct dirent* pDirEnt;
  while ((pDirEnt = readdir(pDir)) != nullptr)
  {
    //        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN && !(Flags & FILESYSTEM_FIND_HIDDEN_FILES))
    //            continue;
    //
    if (pDirEnt->d_name[0] == '.')
    {
      if (pDirEnt->d_name[1] == '\0' || (pDirEnt->d_name[1] == '.' && pDirEnt->d_name[2] == '\0'))
        continue;

      if (!(Flags & FILESYSTEM_FIND_HIDDEN_FILES))
        continue;
    }

    if (ParentPath != nullptr)
      full_path.Format("%s/%s/%s/%s", OriginPath, ParentPath, Path, pDirEnt->d_name);
    else if (Path != nullptr)
      full_path.Format("%s/%s/%s", OriginPath, Path, pDirEnt->d_name);
    else
      full_path.Format("%s/%s", OriginPath, pDirEnt->d_name);

    FILESYSTEM_FIND_DATA outData;
    outData.Attributes = 0;

#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
    struct stat sDir;
    if (stat(full_path, &sDir) < 0)
      continue;

#else
    struct stat64 sDir;
    if (stat64(full_path, &sDir) < 0)
      continue;
#endif

    if (S_ISDIR(sDir.st_mode))
    {
      if (Flags & FILESYSTEM_FIND_RECURSIVE)
      {
        // recurse into this directory
        if (ParentPath != nullptr)
        {
          std::string recursiveDir = StringUtil::StdStringFromFormat("%s/%s", ParentPath, Path);
          nFiles += RecursiveFindFiles(OriginPath, recursiveDir.c_str(), pDirEnt->d_name, Pattern, Flags, pResults);
        }
        else
        {
          nFiles += RecursiveFindFiles(OriginPath, Path, pDirEnt->d_name, Pattern, Flags, pResults);
        }
      }

      if (!(Flags & FILESYSTEM_FIND_FOLDERS))
        continue;

      outData.Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
      if (!(Flags & FILESYSTEM_FIND_FILES))
        continue;
    }

    outData.Size = static_cast<u64>(sDir.st_size);
    outData.ModificationTime.SetUnixTimestamp(static_cast<Timestamp::UnixTimestampValue>(sDir.st_mtime));

    // match the filename
    if (hasWildCards)
    {
      if (!wildCardMatchAll && !StringUtil::WildcardMatch(pDirEnt->d_name, Pattern))
        continue;
    }
    else
    {
      if (std::strcmp(pDirEnt->d_name, Pattern) != 0)
        continue;
    }

    // add file to list
    // TODO string formatter, clean this mess..
    if (!(Flags & FILESYSTEM_FIND_RELATIVE_PATHS))
    {
      outData.FileName = std::string(full_path.GetCharArray());
    }
    else
    {
      if (ParentPath != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s\\%s", ParentPath, Path, pDirEnt->d_name);
      else if (Path != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s", Path, pDirEnt->d_name);
      else
        outData.FileName = pDirEnt->d_name;
    }

    nFiles++;
    pResults->push_back(std::move(outData));
  }

  closedir(pDir);
  return nFiles;
}

bool FindFiles(const char* Path, const char* Pattern, u32 Flags, FindResultsArray* pResults)
{
  // has a path
  if (Path[0] == '\0')
    return false;

  // clear result array
  if (!(Flags & FILESYSTEM_FIND_KEEP_ARRAY))
    pResults->clear();

#ifdef __ANDROID__
  if (IsUriPath(Path) && UriHelpersAreAvailable())
    return FindUriFiles(Path, Pattern, Flags, pResults);
#endif

  // enter the recursive function
  return (RecursiveFindFiles(Path, nullptr, nullptr, Pattern, Flags, pResults) > 0);
}

bool StatFile(const char* Path, FILESYSTEM_STAT_DATA* pStatData)
{
  // has a path
  if (Path[0] == '\0')
    return false;

#ifdef __ANDROID__
  if (IsUriPath(Path) && UriHelpersAreAvailable())
    return StatUriFile(Path, pStatData);
#endif

    // stat file
#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
  struct stat sysStatData;
  if (stat(Path, &sysStatData) < 0)
#else
  struct stat64 sysStatData;
  if (stat64(Path, &sysStatData) < 0)
#endif
    return false;

  // parse attributes
  pStatData->Attributes = 0;
  if (S_ISDIR(sysStatData.st_mode))
    pStatData->Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;

  // parse times
  pStatData->ModificationTime.SetUnixTimestamp((Timestamp::UnixTimestampValue)sysStatData.st_mtime);

  // parse size
  if (S_ISREG(sysStatData.st_mode))
    pStatData->Size = static_cast<u64>(sysStatData.st_size);
  else
    pStatData->Size = 0;

  // ok
  return true;
}

bool StatFile(std::FILE* fp, FILESYSTEM_STAT_DATA* pStatData)
{
  int fd = fileno(fp);
  if (fd < 0)
    return false;

    // stat file
#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
  struct stat sysStatData;
  if (fstat(fd, &sysStatData) < 0)
#else
  struct stat64 sysStatData;
  if (fstat64(fd, &sysStatData) < 0)
#endif
    return false;

  // parse attributes
  pStatData->Attributes = 0;
  if (S_ISDIR(sysStatData.st_mode))
    pStatData->Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;

  // parse times
  pStatData->ModificationTime.SetUnixTimestamp((Timestamp::UnixTimestampValue)sysStatData.st_mtime);

  // parse size
  if (S_ISREG(sysStatData.st_mode))
    pStatData->Size = static_cast<u64>(sysStatData.st_size);
  else
    pStatData->Size = 0;

  // ok
  return true;
}

bool FileExists(const char* Path)
{
  // has a path
  if (Path[0] == '\0')
    return false;

#ifdef __ANDROID__
  if (IsUriPath(Path) && UriHelpersAreAvailable())
  {
    FILESYSTEM_STAT_DATA sd;
    return (StatUriFile(Path, &sd) && (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY) == 0);
  }
#endif

  // stat file
#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
  struct stat sysStatData;
  if (stat(Path, &sysStatData) < 0)
#else
  struct stat64 sysStatData;
  if (stat64(Path, &sysStatData) < 0)
#endif
    return false;

  if (S_ISDIR(sysStatData.st_mode))
    return false;
  return true;
}

bool DirectoryExists(const char* Path)
{
  // has a path
  if (Path[0] == '\0')
    return false;

#ifdef __ANDROID__
  if (IsUriPath(Path) && UriHelpersAreAvailable())
  {
    FILESYSTEM_STAT_DATA sd;
    return (StatUriFile(Path, &sd) && (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY) != 0);
  }
#endif

  // stat file
#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
  struct stat sysStatData;
  if (stat(Path, &sysStatData) < 0)
#else
  struct stat64 sysStatData;
  if (stat64(Path, &sysStatData) < 0)
#endif
    return false;

  if (S_ISDIR(sysStatData.st_mode))
    return true;
  else
    return false;
}

bool CreateDirectory(const char* Path, bool Recursive)
{
  u32 i;
  int lastError;

  // has a path
  if (Path[0] == '\0')
    return false;

  // try just flat-out, might work if there's no other segments that have to be made
  if (mkdir(Path, 0777) == 0)
    return true;

  // check error
  lastError = errno;
  if (lastError == EEXIST)
  {
    // check the attributes
    struct stat sysStatData;
    if (stat(Path, &sysStatData) == 0 && S_ISDIR(sysStatData.st_mode))
      return true;
    else
      return false;
  }
  else if (lastError == ENOENT)
  {
    // part of the path does not exist, so we'll create the parent folders, then
    // the full path again. allocate another buffer with the same length
    u32 pathLength = static_cast<u32>(std::strlen(Path));
    char* tempStr = (char*)alloca(pathLength + 1);

    // create directories along the path
    for (i = 0; i < pathLength; i++)
    {
      if (Path[i] == '/')
      {
        tempStr[i] = '\0';
        if (mkdir(tempStr, 0777) < 0)
        {
          lastError = errno;
          if (lastError != EEXIST) // fine, continue to next path segment
            return false;
        }
      }

      tempStr[i] = Path[i];
    }

    // re-create the end if it's not a separator, check / as well because windows can interpret them
    if (Path[pathLength - 1] != '/')
    {
      if (mkdir(Path, 0777) < 0)
      {
        lastError = errno;
        if (lastError != EEXIST)
          return false;
      }
    }

    // ok
    return true;
  }
  else
  {
    // unhandled error
    return false;
  }
}

bool DeleteFile(const char* Path)
{
  if (Path[0] == '\0')
    return false;

  struct stat sysStatData;
  if (stat(Path, &sysStatData) != 0 || S_ISDIR(sysStatData.st_mode))
    return false;

  return (unlink(Path) == 0);
}

bool RenamePath(const char* OldPath, const char* NewPath)
{
  if (OldPath[0] == '\0' || NewPath[0] == '\0')
    return false;

  if (rename(OldPath, NewPath) != 0)
  {
    Log_ErrorPrintf("rename('%s', '%s') failed: %d", OldPath, NewPath, errno);
    return false;
  }

  return true;
}
#endif

} // namespace FileSystem
