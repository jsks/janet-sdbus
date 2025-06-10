(import ./native :prefix "" :export true)

(defn call-method
  ```
  Send a method call to a D-Bus service

  If the method expects arguments, the first rest argument must be a
  D-Bus signature.
  ```
  [bus destination path interface method & rest]
  (def signature (first rest))
  (def msg (message-new-method-call bus destination path interface method))
  (unless (or (nil? signature) (empty? signature))
    (message-append msg signature ;(slice rest 1)))
  (-> (call bus msg)
      message-read-all))

(defn get-property
  `Get a property from a D-Bus service`
  [bus destination path interface property]
  (def msg (message-new-method-call bus destination path
                                    "org.freedesktop.DBus.Properties" "Get"))
  (message-append msg "ss" interface property)
  (-> (call bus msg)
      message-read-all))

(defn set-property
  ```
  Set a property on a D-Bus service.

  `variant` must a D-Bus variant encoded as a Janet tuple with two
  members: a valid D-Bus signature string and a corresponding value.
  ```
  [bus destination path interface property variant]
  (def msg (message-new-method-call bus destination path
                                    "org.freedesktop.DBus.Properties" "Set"))
  (message-append msg "ssv" interface property variant)
  (call bus msg))

(defn method
  `Returns a function to call a D-Bus method.`
  [bus destination path interface method &opt signature]
  (partial call-method bus destination path interface method signature))

(defn property
  ```
  Returns a function to get or set a D-Bus property.

  The returned function when called without arguments retrieves the
  current value of `property`.  When called with a variant, it sets
  the property to that value.
  ```
  [bus destination path interface property]
  (fn [& variant]
    (if (empty? variant)
      (get-property bus destination path interface property)
      (set-property bus destination path interface property variant))))
