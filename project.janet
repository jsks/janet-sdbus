(declare-project
  :name "sdbus"
  :author "jsks"
  :description "sdbus bindings"
  :version "0.1.0")

### Utility functions
(defn run [& args]
  (def child (os/spawn args :p {:out :pipe}))
  (unless (zero? (os/proc-wait child))
    (errorf "command failed: %p" args))
  (def output (:read (child :out) :all))
  (when (not (nil? output))
    (string/trim output)))

(defn some-suffix? [str & suffixes]
  (some |(string/has-suffix? $ str) suffixes))

(defn find-files [directory & suffixes]
  (->> (map |(string directory "/" $) (os/dir directory))
       (filter |(some-suffix? $ ;suffixes))))

(defn is-gcc? []
  (let [version (run "env" "-u" "LD_PRELOAD" (dyn :cc) "--version")]
    (string/find "gcc" (string/ascii-lower version))))

### CFLAGS/LDFLAGS
(when (= (dyn :build-type) "debug")
  (setdyn :optimize 0))

(def project-cflags @["-pedantic" "-Werror"])
(when (and (not= (dyn :build-type) "release") (is-gcc?))
  (array/push project-cflags "-fanalyzer"))

(when (= (dyn :build-type) "develop")
  (array/push project-cflags "-fsanitize=address"))

### Source files
(declare-source
  :prefix "sdbus"
  :source ["base.janet" "init.janet" "introspect.janet" "proxy.janet"])

(declare-native
  :name "sdbus/native"
  :cflags [;default-cflags ;project-cflags]
  :lflags [;default-ldflags (run "pkg-config" "--libs" "libsystemd")]
  :headers ["src/common.h"]
  :source ["src/async.c"
           "src/bus.c"
           "src/call.c"
           "src/export.c"
           "src/main.c"
           "src/message.c"
           "src/slot.c"])

## Development tasks
(task "fmt" []
  (run "clang-format" "-i" "--Werror" "--style=file" ;(find-files "src" ".c" ".h")))
