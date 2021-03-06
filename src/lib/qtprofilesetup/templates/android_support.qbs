import qbs.File
import qbs.FileInfo
import qbs.ModUtils
import qbs.TextFile
import qbs.Utilities

Module {
    property bool useMinistro: false
    property string qmlRootDir: product.sourceDirectory
    property stringList extraPrefixDirs
    property stringList deploymentDependencies // qmake: ANDROID_DEPLOYMENT_DEPENDENCIES
    property stringList extraPlugins // qmake: ANDROID_EXTRA_PLUGINS
    property bool verboseAndroidDeployQt: false

    property string _androidDeployQtFilePath: FileInfo.joinPaths(_qtInstallDir, "bin",
                                                                 "androiddeployqt")
    property string _qtInstallDir
    property bool _enableSdkSupport: product.type && product.type.contains("android.apk")
                                     && !consoleApplication
    property bool _enableNdkSupport: !product.aggregate || product.multiplexConfigurationId
    property string _templatesBaseDir: FileInfo.joinPaths(_qtInstallDir, "src", "android")
    property string _deployQtOutDir: FileInfo.joinPaths(product.buildDirectory, "deployqt_out")

    Depends { name: "Android.sdk"; condition: _enableSdkSupport }
    Depends { name: "Android.ndk"; condition: _enableNdkSupport }
    Depends { name: "java"; condition: _enableSdkSupport }

    Properties {
        condition: _enableNdkSupport && qbs.toolchain.contains("clang")
        Android.ndk.appStl: "c++_shared"
    }
    Properties {
        condition: _enableNdkSupport && !qbs.toolchain.contains("clang")
        Android.ndk.appStl: "gnustl_shared"
    }
    Properties {
        condition: _enableSdkSupport
        Android.sdk.customManifestProcessing: true
        java._tagJniHeaders: false // prevent rule cycle
    }

    Rule {
        condition: _enableSdkSupport
        multiplex: true
        property stringList inputTags: "android.nativelibrary"
        inputsFromDependencies: inputTags
        inputs: product.aggregate ? [] : inputTags
        Artifact {
            filePath: "androiddeployqt.json"
            fileTags: "qt_androiddeployqt_input"
        }
        prepare: {
            var cmd = new JavaScriptCommand();
            cmd.description = "creating " + output.fileName;
            cmd.sourceCode = function() {
                var theBinary;
                var nativeLibs = inputs["android.nativelibrary"];
                if (nativeLibs.length === 1) {
                    theBinary = nativeLibs[0];
                } else {
                    for (i = 0; i < nativeLibs.length; ++i) {
                        var candidate = nativeLibs[i];
                        if (candidate.fileName.contains(candidate.product.targetName)) {
                            if (theBinary) {
                                throw "Qt applications for Android support only one native binary "
                                        + "per package.\n"
                                        + "In particular, you cannot build a Qt app for more than "
                                        + "one architecture at the same time.";
                            }
                            theBinary = candidate;
                        }
                    }
                }
                var f = new TextFile(output.filePath, TextFile.WriteOnly);
                f.writeLine("{");
                f.writeLine('"description": "This file was generated by qbs to be read by '
                            + 'androiddeployqt and should not be modified by hand.",');
                f.writeLine('"qt": "' + product.Qt.android_support._qtInstallDir + '",');
                f.writeLine('"sdk": "' + product.Android.sdk.sdkDir + '",');
                f.writeLine('"sdkBuildToolsRevision": "' + product.Android.sdk.buildToolsVersion
                            + '",');
                f.writeLine('"ndk": "' + product.Android.sdk.ndkDir + '",');
                var toolPrefix = theBinary.cpp.toolchainTriple;
                var toolchainPrefix = toolPrefix.startsWith("i686-") ? "x86" : toolPrefix;
                f.writeLine('"toolchain-prefix": "' + toolchainPrefix + '",');
                f.writeLine('"tool-prefix": "' + toolPrefix + '",');
                f.writeLine('"toolchain-version": "' + theBinary.Android.ndk.toolchainVersion
                            + '",');
                f.writeLine('"ndk-host": "' + theBinary.Android.ndk.hostArch + '",');
                f.writeLine('"target-architecture": "' + theBinary.Android.ndk.abi + '",');
                f.writeLine('"qml-root-path": "' + product.Qt.android_support.qmlRootDir + '",');
                var deploymentDeps = product.Qt.android_support.deploymentDependencies;
                if (deploymentDeps && deploymentDeps.length > 0)
                    f.writeLine('"deployment-dependencies": "' + deploymentDeps.join() + '",');
                var extraPlugins = product.Qt.android_support.extraPlugins;
                if (extraPlugins && extraPlugins.length > 0)
                    f.writeLine('"android-extra-plugins": "' + extraPlugins.join() + '",');
                var prefixDirs = product.Qt.android_support.extraPrefixDirs;
                if (prefixDirs && prefixDirs.length > 0)
                    f.writeLine('"extraPrefixDirs": ' + JSON.stringify(prefixDirs) + ',');
                if (Array.isArray(product.qmlImportPaths) && product.qmlImportPaths.length > 0)
                    f.writeLine('"qml-import-paths": "' + product.qmlImportPaths.join(',') + '",');
                f.writeLine('"application-binary": "' + theBinary.filePath + '"');
                f.writeLine("}");
                f.close();
            };
            return cmd;
        }
    }

    // We use the manifest template from the Qt installation if and only if the project
    // does not provide a manifest file.
    Rule {
        condition: _enableSdkSupport
        multiplex: true
        requiresInputs: false
        inputs: "android.manifest"
        excludedInputs: "qt.android_manifest"
        outputFileTags: ["android.manifest", "qt.android_manifest"]
        outputArtifacts: {
            if (inputs["android.manifest"])
                return [];
            return [{
                filePath: "qt_manifest/AndroidManifest.xml",
                fileTags: ["android.manifest", "qt.android_manifest"]
            }];
        }
        prepare: {
            var cmd = new JavaScriptCommand();
            cmd.description = "copying Qt Android manifest template";
            cmd.sourceCode = function() {
                File.copy(FileInfo.joinPaths(product.Qt.android_support._templatesBaseDir,
                          "templates", "AndroidManifest.xml"), output.filePath);
            };
            return cmd;
        }
    }

    Rule {
        condition: _enableSdkSupport
        multiplex: true
        inputs: ["qt_androiddeployqt_input", "android.manifest_processed"]
        outputFileTags: [
            "android.manifest_final", "android.resources", "android.assets", "bundled_jar",
            "android.deployqt_list",
        ]
        outputArtifacts: {
            var artifacts = [
                {
                    filePath: "AndroidManifest.xml",
                    fileTags: "android.manifest_final"
                },
                {
                    filePath: product.Qt.android_support._deployQtOutDir + "/res/values/libs.xml",
                    fileTags: "android.resources"
                },
                {
                    filePath: product.Qt.android_support._deployQtOutDir
                              + "/res/values/strings.xml",
                    fileTags: "android.resources"
                },
                {
                    filePath: product.Qt.android_support._deployQtOutDir + "/assets/.dummy",
                    fileTags: "android.assets"
                },
                {
                    filePath: "deployqt.list",
                    fileTags: "android.deployqt_list"
                },

            ];
            if (!product.Qt.android_support.useMinistro) {
                artifacts.push({
                    filePath: FileInfo.joinPaths(product.java.classFilesDir, "QtAndroid.jar"),
                    fileTags: ["bundled_jar"]
                });
            }
            return artifacts;
        }
        prepare: {
            var copyCmd = new JavaScriptCommand();
            copyCmd.description = "copying Qt resource templates";
            copyCmd.sourceCode = function() {
                File.copy(inputs["android.manifest_processed"][0].filePath,
                          product.Qt.android_support._deployQtOutDir + "/AndroidManifest.xml");
                File.copy(product.Qt.android_support._templatesBaseDir + "/java/res",
                          product.Qt.android_support._deployQtOutDir + "/res");
                File.copy(product.Qt.android_support._templatesBaseDir
                          + "/templates/res/values/libs.xml",
                          product.Qt.android_support._deployQtOutDir + "/res/values/libs.xml");
                try {
                    File.remove(FileInfo.path(outputs["android.assets"][0].filePath));
                } catch (e) {
                }
            };
            var androidDeployQtArgs = [
                "--output", product.Qt.android_support._deployQtOutDir,
                "--input", inputs["qt_androiddeployqt_input"][0].filePath, "--aux-mode",
                "--deployment", product.Qt.android_support.useMinistro ? "ministro" : "bundled",
                "--android-platform", product.Android.sdk.platform,
            ];
            if (product.Qt.android_support.verboseAndroidDeployQt)
                args.push("--verbose");
            var androidDeployQtCmd = new Command(
                        product.Qt.android_support._androidDeployQtFilePath, androidDeployQtArgs);
            androidDeployQtCmd.description = "running androiddeployqt";

            // We do not want androiddeployqt to write directly into our APK base dir, so
            // we ran it on an isolated directory and now we move stuff over.
            // We remember the files for which we do that, so if the next invocation
            // of androiddeployqt creates fewer files, the other ones are removed from
            // the APK base dir.
            var moveCmd = new JavaScriptCommand();
            moveCmd.description = "processing androiddeployqt outout";
            moveCmd.sourceCode = function() {
                File.move(product.Qt.android_support._deployQtOutDir + "/AndroidManifest.xml",
                          outputs["android.manifest_final"][0].filePath);
                var libsDir = product.Qt.android_support._deployQtOutDir + "/libs";
                var libDir = product.Android.sdk.apkContentsDir + "/lib";
                var listFilePath = outputs["android.deployqt_list"][0].filePath;
                var oldLibs = [];
                try {
                    var listFile = new TextFile(listFilePath, TextFile.ReadOnly);
                    var listFileLine = listFile.readLine();
                    while (listFileLine) {
                        oldLibs.push(listFileLine);
                        listFileLine = listFile.readLine();
                    }
                    listFile.close();
                } catch (e) {
                }
                listFile = new TextFile(listFilePath, TextFile.WriteOnly);
                var newLibs = [];
                var moveLibFiles = function(prefix) {
                    var fullSrcPrefix = FileInfo.joinPaths(libsDir, prefix);
                    var files = File.directoryEntries(fullSrcPrefix, File.Files);
                    for (var i = 0; i < files.length; ++i) {
                        var file = files[i];
                        var srcFilePath = FileInfo.joinPaths(fullSrcPrefix, file);
                        var targetFilePath;
                        if (file.endsWith(".jar"))
                            targetFilePath = FileInfo.joinPaths(product.java.classFilesDir, file);
                        else
                            targetFilePath = FileInfo.joinPaths(libDir, prefix, file);
                        File.move(srcFilePath, targetFilePath);
                        listFile.writeLine(targetFilePath);
                        newLibs.push(targetFilePath);
                    }
                    var dirs = File.directoryEntries(fullSrcPrefix,
                                                     File.Dirs | File.NoDotAndDotDot);
                    for (i = 0; i < dirs.length; ++i)
                        moveLibFiles(FileInfo.joinPaths(prefix, dirs[i]));
                };
                moveLibFiles("");
                listFile.close();
                for (i = 0; i < oldLibs.length; ++i) {
                    if (!newLibs.contains(oldLibs[i]))
                        File.remove(oldLibs[i]);
                }
            };
            return [copyCmd, androidDeployQtCmd, moveCmd];
        }
    }

    Group {
        condition: Qt.android_support._enableSdkSupport
        name: "helper sources from qt"
        prefix: Qt.android_support._templatesBaseDir + "/java/"
        Android.sdk.aidlSearchPaths: prefix + "src"
        files: [
            "**/*.java",
            "**/*.aidl",
        ]
    }

    validate: {
        if (Utilities.versionCompare(version, "5.12") < 0)
            throw ModUtils.ModuleError("Cannot use Qt " + version + " with Android. "
                + "Version 5.12 or later is required.");
    }
}
