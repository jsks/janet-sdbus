(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))

(def env {:Add (sdbus/method :: "ii" -> "i" [x y] (+ x y))
          :Constant (sdbus/create-property @[42 32] :sig "ai" :flags "r")
          :Mutable (sdbus/create-property "Hello" :sig "s" :flags "w")})

(sdbus/request-name bus "org.janet.UnitTests")
(sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env)

(def spec (sdbus/introspect bus "org.janet.UnitTests" "/org/janet/UnitTests"))
(def local (sdbus/proxy bus spec :org.janet.UnitTests))

(assert (= (:Add local 1 2) 3))
(assert (= (:Add local 40 200) 240))
(assert (deep= (:Constant local) @[42 32]))
(assert (= (:Mutable local) "Hello"))

(:Mutable local "World")
(assert (= (:Mutable local) "World"))
(assert-error "Wrong type" (:Mutable local 12))
(assert-error "Constant property" (:Constant local @[12 3 45]))

(sdbus/close-bus bus)

(end-suite)
