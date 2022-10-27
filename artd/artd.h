/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_ARTD_ARTD_H_
#define ART_ARTD_ARTD_H_

#include <sys/types.h>

#include <csignal>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aidl/com/android/server/art/BnArtd.h"
#include "aidl/com/android/server/art/BnArtdCancellationSignal.h"
#include "android-base/result.h"
#include "android-base/thread_annotations.h"
#include "android/binder_auto_utils.h"
#include "exec_utils.h"
#include "oat_file_assistant_context.h"
#include "tools/cmdline_builder.h"
#include "tools/system_properties.h"

namespace art {
namespace artd {

class ArtdCancellationSignal : public aidl::com::android::server::art::BnArtdCancellationSignal {
 public:
  explicit ArtdCancellationSignal(std::function<int(pid_t, int)> kill_func)
      : kill_(std::move(kill_func)) {}

  ndk::ScopedAStatus cancel() override;

  ndk::ScopedAStatus getType(int64_t* _aidl_return) override;

 private:
  std::mutex mu_;
  // True if cancellation has been signaled.
  bool is_cancelled_ GUARDED_BY(mu_) = false;
  // The pids of currently running child processes that are bound to this signal.
  std::unordered_set<pid_t> pids_ GUARDED_BY(mu_);

  std::function<int(pid_t, int)> kill_;

  friend class Artd;
};

class Artd : public aidl::com::android::server::art::BnArtd {
 public:
  explicit Artd(std::unique_ptr<art::tools::SystemProperties> props =
                    std::make_unique<art::tools::SystemProperties>(),
                std::unique_ptr<ExecUtils> exec_utils = std::make_unique<ExecUtils>(),
                std::function<int(pid_t, int)> kill_func = kill)
      : props_(std::move(props)), exec_utils_(std::move(exec_utils)), kill_(std::move(kill_func)) {}

  ndk::ScopedAStatus isAlive(bool* _aidl_return) override;

  ndk::ScopedAStatus deleteArtifacts(
      const aidl::com::android::server::art::ArtifactsPath& in_artifactsPath,
      int64_t* _aidl_return) override;

  ndk::ScopedAStatus getOptimizationStatus(
      const std::string& in_dexFile,
      const std::string& in_instructionSet,
      const std::string& in_classLoaderContext,
      aidl::com::android::server::art::GetOptimizationStatusResult* _aidl_return) override;

  ndk::ScopedAStatus isProfileUsable(const aidl::com::android::server::art::ProfilePath& in_profile,
                                     const std::string& in_dexFile,
                                     bool* _aidl_return) override;

  ndk::ScopedAStatus copyAndRewriteProfile(
      const aidl::com::android::server::art::ProfilePath& in_src,
      aidl::com::android::server::art::OutputProfile* in_dst,
      const std::string& in_dexFile,
      bool* _aidl_return) override;

  ndk::ScopedAStatus commitTmpProfile(
      const aidl::com::android::server::art::ProfilePath::TmpProfilePath& in_profile) override;

  ndk::ScopedAStatus deleteProfile(
      const aidl::com::android::server::art::ProfilePath& in_profile) override;

  ndk::ScopedAStatus getProfileVisibility(
      const aidl::com::android::server::art::ProfilePath& in_profile,
      aidl::com::android::server::art::FileVisibility* _aidl_return) override;

  ndk::ScopedAStatus mergeProfiles(
      const std::vector<aidl::com::android::server::art::ProfilePath>& in_profiles,
      const std::optional<aidl::com::android::server::art::ProfilePath>& in_referenceProfile,
      aidl::com::android::server::art::OutputProfile* in_outputProfile,
      const std::string& in_dexFile,
      bool* _aidl_return) override;

  ndk::ScopedAStatus getArtifactsVisibility(
      const aidl::com::android::server::art::ArtifactsPath& in_artifactsPath,
      aidl::com::android::server::art::FileVisibility* _aidl_return) override;

  ndk::ScopedAStatus getDexFileVisibility(
      const std::string& in_dexFile,
      aidl::com::android::server::art::FileVisibility* _aidl_return) override;

  ndk::ScopedAStatus getDexoptNeeded(
      const std::string& in_dexFile,
      const std::string& in_instructionSet,
      const std::optional<std::string>& in_classLoaderContext,
      const std::string& in_compilerFilter,
      int32_t in_dexoptTrigger,
      aidl::com::android::server::art::GetDexoptNeededResult* _aidl_return) override;

  ndk::ScopedAStatus dexopt(
      const aidl::com::android::server::art::OutputArtifacts& in_outputArtifacts,
      const std::string& in_dexFile,
      const std::string& in_instructionSet,
      const std::optional<std::string>& in_classLoaderContext,
      const std::string& in_compilerFilter,
      const std::optional<aidl::com::android::server::art::ProfilePath>& in_profile,
      const std::optional<aidl::com::android::server::art::VdexPath>& in_inputVdex,
      aidl::com::android::server::art::PriorityClass in_priorityClass,
      const aidl::com::android::server::art::DexoptOptions& in_dexoptOptions,
      const std::shared_ptr<aidl::com::android::server::art::IArtdCancellationSignal>&
          in_cancellationSignal,
      aidl::com::android::server::art::DexoptResult* _aidl_return) override;

  ndk::ScopedAStatus createCancellationSignal(
      std::shared_ptr<aidl::com::android::server::art::IArtdCancellationSignal>* _aidl_return)
      override;

  android::base::Result<void> Start();

 private:
  android::base::Result<OatFileAssistantContext*> GetOatFileAssistantContext()
      EXCLUDES(ofa_context_mu_);

  android::base::Result<const std::vector<std::string>*> GetBootImageLocations();

  android::base::Result<const std::vector<std::string>*> GetBootClassPath();

  bool UseJitZygote();

  bool DenyArtApexDataFiles();

  android::base::Result<int> ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                                               int timeout_sec,
                                               const ExecCallbacks& callbacks = ExecCallbacks(),
                                               ProcessStat* stat = nullptr) const;

  android::base::Result<std::string> GetProfman();

  android::base::Result<std::string> GetArtExec();

  bool ShouldUseDex2Oat64();

  android::base::Result<std::string> GetDex2Oat();

  bool ShouldCreateSwapFileForDexopt();

  void AddCompilerConfigFlags(const std::string& instruction_set,
                              const std::string& compiler_filter,
                              aidl::com::android::server::art::PriorityClass priority_class,
                              const aidl::com::android::server::art::DexoptOptions& dexopt_options,
                              /*out*/ art::tools::CmdlineBuilder& args);

  void AddPerfConfigFlags(aidl::com::android::server::art::PriorityClass priority_class,
                          /*out*/ art::tools::CmdlineBuilder& args);

  std::optional<std::vector<std::string>> cached_boot_image_locations_;
  std::optional<std::vector<std::string>> cached_boot_class_path_;
  std::optional<std::string> cached_apex_versions_;
  std::optional<bool> cached_use_jit_zygote_;
  std::optional<bool> cached_deny_art_apex_data_files_;

  std::mutex ofa_context_mu_;
  std::unique_ptr<OatFileAssistantContext> ofa_context_ GUARDED_BY(ofa_context_mu_);

  std::unique_ptr<art::tools::SystemProperties> props_;
  std::unique_ptr<ExecUtils> exec_utils_;
  std::function<int(pid_t, int)> kill_;
};

}  // namespace artd
}  // namespace art

#endif  // ART_ARTD_ARTD_H_
