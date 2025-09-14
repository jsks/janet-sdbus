# Server-side script for server-client example showing how to publish
#  an interface and listen for requests.
#
# Usage:
#   $ janet server.janet
###

(import sdbus)

(def bus (sdbus/open-user-bus))

(def global-store @{})

(defn emit-key-added [key]
  (sdbus/emit-signal (dyn :sdbus/bus) (dyn :sdbus/path) (dyn :sdbus/interface)
                     "KeyAdded" "s" (string/format "Key added: %d" key)))

(defn emit-key-removed [key]
  (sdbus/emit-signal (dyn :sdbus/bus) (dyn :sdbus/path) (dyn :sdbus/interface)
                     "KeyRemoved" "s" (string/format "Key removed: %d" key)))

(defn get-fn [key]
  (def value (global-store key))
  (when (nil? value)
    (error "Key not found!"))
  value)

(defn add-fn [key value]
  (set (global-store key) value)
  (emit-key-added key))

(defn remove-fn [key]
  (set (global-store key) nil)
  (emit-key-removed key))

(defn clear-fn []
  (eachk key global-store
    (emit-key-removed key))
  (table/clear global-store))

(defn count-fn []
  (length global-store))

(def env { # Methods
          :Get (sdbus/method "i" "v" get-fn)
          :Add (sdbus/method "iv" "" add-fn)
          :Remove (sdbus/method "i" "" remove-fn)
          :Clear (sdbus/method "" "" clear-fn)
          :Count (sdbus/method "" "i" count-fn)

          # Properties
          :Store (sdbus/property "a{iv}" global-store :h)

          # Signals
          :KeyAdded (sdbus/signal "s")
          :KeyRemoved (sdbus/signal "s")})

(if-not (nil? (sdbus/request-name bus "org.janet.example" :n))
  (error "Unable to acquire name: org.janet.example"))

# Publish the interface and immediately start asynchronously listening
# to events. Note, this blocks program exit until the bus connection
# is explicitly closed.
(sdbus/export bus "/org/janet/example" "org.janet.example" env)

(print "Listening for D-Bus requests on org.janet.example... (Ctrl-C to exit)")
