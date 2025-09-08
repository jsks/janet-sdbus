# Example script showing how to retrieve and work with introspection
# data for a D-Bus object. Mimics the output of `busctl introspect`
# for /org/freedesktop/DBus.
#
# Usage:
#   $ janet introspect.janet
###

(import sdbus)

(defn fmt-string [max-length]
  ```
  Return a printf-style format string with appropriate padding for the
  first column containing the interface members.
  ```
  (string/format "%%-%ds %%-10s %%-10s %%-15s %%s" (inc max-length)))

(defn bold-fmt-string [max-length]
  "Similar to `fmt-string`, but bold the first column for interface names."
  (string/format "\x1B[1m%%-%ds\x1B[0m %%-10s %%-10s %%-15s %%s" (inc max-length)))

(defn format-annotations
  "Return a formatted string for annotations attached to a member."
  [annotations]
  (def flags (seq [{:name name :value value} :in annotations
                   :unless (= value "false")]
               (when-let [start (last (string/find-all "." name))]
                 (string/slice name (inc start)))))
  (if (empty? flags) "-"
    (string/join flags ", ")))

(defn format-args
  "Return a formatted string for method/signal types."
  [args]
  (def types (map |($ :type) args))
  (if (empty? types) "-"
    (string/join types "")))

(defn format-method [name method]
  "Return a description array for a method member."
  [name "method" (format-args (method :in)) (format-args (method :out))
   (format-annotations (method :annotations))])

(defn format-property [name property]
  "Return a description array for a property member."
  (def attributes (format-annotations (property :annotations)))
  (def access (case (property :access)
                "read"      nil
                "readwrite" "Writable"
                "write"     "Write-only"))
  (def flags (cond
               (and (empty? attributes) (nil? access)) "-"
               (nil? access) attributes
               (empty? attributes) access
               (string/format "%s, %s" access attributes)))
  [name "property" (property :type) "-" flags])

(defn format-signal [name signal]
  "Return a description array for a signal member."
  [name "signal" (format-args (signal :args)) "-"
   (format-annotations (signal :annotations))])

(defn filter-members
  ```
  For each interface member matching `kind`, return an array
  containing the name, intput signature, output signature, and
  annotated flags.
  ```
  [spec interface kind]
  (def members (get-in spec [:interfaces interface :members]))
  (seq [[name data] :pairs members
        :when (= (data :kind) kind)
        :let [prefixed-name (string "." name)]]
    (case kind
      'method (format-method prefixed-name data)
      'property (format-property prefixed-name data)
      'signal (format-signal prefixed-name data))))

(defn print-members
  "Print a formatted row for each member in interface matching `kind`."
  [spec interface kind max-length]
  (def members (-> (filter-members spec interface kind) sort))
  (each member members
    (printf (fmt-string max-length) ;member)))

(with [bus (sdbus/open-system-bus)]
  (def spec (sdbus/introspect bus "org.freedesktop.DBus" "/org/freedesktop/DBus"))
  (def interfaces (-> (spec :interfaces) keys sort))

  # Right-wise padding for the first column
  (def lens (seq [interface :in interfaces]
              (->> (get-in spec [:interfaces interface :members])
                   keys
                   (map length))))
  (def max-length (max-of (flatten lens)))

  (printf (fmt-string max-length) "NAME" "TYPE" "SIGNATURE" "RESULT/VALUE" "FLAG")
  (loop [interface :in interfaces]
    (printf (bold-fmt-string max-length) interface "interface" "-" "-" "-")

    (each kind '(method property signal)
      (print-members spec interface kind max-length))))
