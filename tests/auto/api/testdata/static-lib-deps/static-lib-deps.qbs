import qbs 1.0

Project {
    StaticLibrary {
        name: "a"

        Depends { name: "cpp" }

        files: [
            "a1.cpp",
            "a2.cpp",
        ]
    }
    StaticLibrary {
        name: "b"

        Depends { name: "cpp" }

        Depends { name: "a" }

        files: [
            "b.cpp",
        ]
    }
    StaticLibrary {
        name: "c"

        Depends { name: "cpp" }

        Depends { name: "a" }

        files: [
            "c.cpp",
        ]
    }
    StaticLibrary {
        name: "d"

        Depends { name: "cpp" }

        Depends { name: "b" }
        Depends { name: "c" }

        files: [
            "d.cpp",
        ]

        Properties {
            condition: qbs.targetOS.contains("windows")
            cpp.defines: ["WITH_SETUPAPI"]
        }
        Properties {
            condition: qbs.targetOS.contains("macos")
            cpp.defines: ["WITH_LEX_YACC"]
        }
        Properties {
            condition: qbs.targetOS.contains("linux")
            cpp.defines: ["WITH_PTHREAD"]
        }

        Export {
            Depends { name: "cpp" }
            Properties {
                condition: qbs.targetOS.contains("linux")
                cpp.staticLibraries: ["pthread"]
            }
            Properties {
                condition: qbs.targetOS.contains("macos")
                cpp.staticLibraries: ["l", "y"]
            }
            Properties {
                condition: qbs.targetOS.contains("windows")
                cpp.staticLibraries: ["setupapi"]
            }
        }
    }
    StaticLibrary {
        name: "e"

        Depends { name: "cpp" }

        Depends { name: "d" }

        files: [
            "e.cpp",
        ]
    }
    CppApplication {
        name: "staticLibDeps"
        type: "application"
        consoleApplication: true

        Depends { name: "e" }

        Properties {
            condition: qbs.targetOS.contains("linux")
            cpp.driverFlags: ["-static"]
        }

        files: [
            "main.cpp",
        ]
    }
}
