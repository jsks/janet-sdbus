(use ../vendor/test)
(import sdbus/native :as sdbus)

(start-suite)

(setdyn :bus (sdbus/open-system-bus))

(defn method-call-stub []
  (sdbus/message-new-method-call
    (dyn :bus)
    "org.freedesktop.DBus"
    "/org/freedesktop/DBus"
    "org.freedesktop.DBus.Peer"
    "GetMachineId"))

(defn from-message [sig & args]
  (def msg (method-call-stub))
  (sdbus/message-append msg sig ;args)
  (sdbus/message-seal msg)
  (sdbus/message-read-all msg))

# Append basic types
(defmacro test-basic [dbus-type value janet-type]
  (with-syms [$dbus-type $value $janet-type $out]
    ~(let [,$dbus-type ,dbus-type
           ,$value ,value
           ,$janet-type ,janet-type
           ,$out (from-message ,$dbus-type ,$value)]
       (assert (= (type ,$out) ,$janet-type))
       (assert (= ,$out ,$value)))))

(test-basic "b" true :boolean)            # Boolean
(test-basic "n" -30 :number)              # Int16
(test-basic "q" 40 :number)               # UInt16
(test-basic "i" 50 :number)               # Int32
(test-basic "u" 60 :number)               # UInt32
(test-basic "x" (int/s64 2) :core/s64)    # Int64
(test-basic "t" (int/u64 3444) :core/u64) # UInt64
(test-basic "d" -10.2 :number)            # Double

(test-basic "s" "Hello World" :string)           # String
(test-basic "o" "/org/freedesktop/DBus" :string) # Object path
(test-basic "g" "org.freedesktop.DBus" :string)  # Interface name

(assert-error "Test invalid boolean" (from-message "b" 10))
(assert-error "Decimal to integer" (from-message "i" 3.14))
(assert-error "Integer overflow" (from-message "n" 100000))
(assert-error "Signed to unsigned" (from-message "u" -10))
(assert-error "Invalid input to string" (from-message "s" 10))

# Multiple inputs
(assert (deep= (from-message "sis" "Hello" 42 "World") @["Hello" 42 "World"]))

# Variant type
(assert (deep= (from-message "v" ["s" "Hello World"]) ["s" "Hello World"]))
(assert (deep= (from-message "vv" ["u" 1] ["u" 2]) @[["u" 1] ["u" 2]]))

(assert-error "Variant with multiple types" (from-message "v" ["i" 10 "s" "Hello World"]))

# Struct type
(assert (deep= (from-message "(d)" [0.00001]) [0.00001]))
(assert (deep= (from-message "(sii)" ["Hello" 42 100]) ["Hello" 42 100]))
(assert (deep= (from-message "(i(v))" [1 [["(i)" [2]]]]) [1 [["(i)" [2]]]]))
(assert (deep= (from-message "i(i)i" 1 [1] 2) @[1 [1] 2]))

(assert-error "Empty struct" (from-message "()" []))
(assert-error "Mismatched struct lengths" (from-message "(ii)" [1 2 3]))

# Array type
(assert (deep= (from-message "ab" @[true false]) @[true false]))
(assert (deep= (from-message "a(si)" @[["Hello" 1] ["World" 2]]) @[["Hello" 1] ["World" 2]]))
(assert (deep= (from-message "aad" @[@[0.1] @[0.2]]) @[@[0.1] @[0.2]]))
(assert (deep= (from-message "aii" @[1 2] 3) @[@[1 2] 3]))
(assert (deep= (from-message "v" ["as" @["Hello" "World"]]) ["as" @["Hello" "World"]]))

(assert-error "Empty array" (from-message "a" @[]))
(assert-error "Be strict about array input" (from-message "as" ["Hello" "World"]))

# Dictionary type
(assert (deep= (from-message "a{is}" @{1 "val" 2 "val2"}) @{1 "val" 2 "val2"}))
(assert (deep= (from-message "a{s(ii)}" @{"key" [1 2]}) @{"key" [1 2]}))
(assert (deep= (from-message "aa{ii}" @[@{1 2}]) @[@{1 2}]))
(assert (deep= (from-message "a{ii}ai" @{1 2} @[1 2]) @[@{1 2} @[1 2]]))

(assert-error "Incomplete dictionary signature" (from-message "a{i}" @{}))
(assert-error "Empty dictionary" (from-message "a{ii}" @{}))
(assert-error "Variant as key" (from-message "a{vi}" @{["s" "key"] 1}))

# Misc. checks
(assert-error "Missing arguments" (from-message "ii" 1))
(assert-error "Excessive arguments" (from-message "ii" 1 2 3))

(end-suite)
