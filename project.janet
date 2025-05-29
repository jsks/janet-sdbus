(declare-project
  :name "sdbus"
  :description "sdbus bindings"
  :version "0.0.1")

(declare-source
  :prefix "sdbus"
  :source ["janet/init.janet"
           "janet/methods.janet"])

(defn pkg-config [& args]
  (def child (os/spawn ["pkg-config" ;args] :p {:out :pipe}))
  (when (not (os/proc-wait child))
    (errorf "pkg-config failed to start with arguments: %p" args))
  (def out (-> (:read (child :out) :all) (string/trim)))
  (if (empty? out) [] (string/split " " out)))

(def debug? (= (dyn :build-type) "debug"))

(when debug?
  (setdyn :optimize 0))

(def sanitizer-cflags
  (if debug?
    ["-fsanitize=address" "-fno-omit-frame-pointer"]
    []))

(def project-cflags
  (tuple/join default-cflags sanitizer-cflags))

(def project-ldflags
  (tuple/join default-ldflags (pkg-config "--libs" "libsystemd")))

(declare-native
  :name "sdbus-native"
  :cflags project-cflags
  :lflags project-ldflags
  :headers ["src/common.h"]
  :source ["src/bus.c"
           "src/main.c"
           "src/message.c"])
