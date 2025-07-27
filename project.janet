(declare-project
  :name "sdbus"
  :description "sdbus bindings"
  :version "0.1.0")

### Utility functions
(defn run [& args]
  (def child (os/spawn args :p {:out :pipe}))
  (unless (zero? (os/proc-wait child))
    (errorf "command failed: %p" args))
  (def output (:read (child :out) :all))
  (if (or (nil? output) (empty? output))
    []
    (->> (string/trim output)
         (string/split " "))))

(defn some-suffix? [str & suffixes]
  (some |(string/has-suffix? $ str) suffixes))

(defn find-files [directory & suffixes]
  (->> (map |(string directory "/" $) (os/dir directory))
       (filter |(some-suffix? $ ;suffixes))))

### CFLAGS/LDFLAGS
(when (= (dyn :build-type) "debug")
  (setdyn :optimize 0))

(def sanitizer-cflags
  (if (= (dyn :build-type) "debug")
    ["-fsanitize=address" "-fno-omit-frame-pointer"]
    []))

(def project-cflags
  (tuple/join default-cflags sanitizer-cflags))

(def project-ldflags
  (tuple/join default-ldflags (run "pkg-config" "--libs" "libsystemd")))

### Source files
(declare-source
  :prefix "sdbus"
  :source ["init.janet"])

(declare-native
  :name "sdbus/native"
  :cflags project-cflags
  :lflags project-ldflags
  :headers ["src/common.h"]
  :source ["src/bus.c"
           "src/call.c"
           "src/main.c"
           "src/message.c"])

(task "fmt" []
  (run "clang-format" "-i" "--Werror" "--style=file" ;(find-files "src" ".c" ".h")))
