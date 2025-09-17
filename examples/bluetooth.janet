# Listen for newly connected Bluetooth devices (BlueZ) and send a
# desktop notification via org.freedesktop.Notifications.
#
# Requirements:
# - BlueZ running on the system bus (org.bluez)
# - A notification daemon implementing org.freedesktop.Notifications
#
# Usage:
#   $ janet examples/bluetooth.janet
###

(import sdbus)

(defn connected?
  "Returns true if the Connection property for a device changes to 'true'"
  [property]
  (match property
    @{"Connected" ["b" status]} status
    false))

(defn device-name [bus path]
  "Get device name registered with BlueZ"
  (-> (sdbus/get-property bus "org.bluez" path "org.bluez.Device1" "Name")
      (get 1)))

(def sys (sdbus/open-system-bus))
(def user (sdbus/open-user-bus))

(def spec (sdbus/introspect user "org.freedesktop.Notifications"
                                 "/org/freedesktop/Notifications"))
(def notifications (sdbus/proxy user spec :org.freedesktop.Notifications))

# Use subscribe-property-changed directly for more fine-grained
# control over the match rule. In this case, we want all property
# changes to the `org.bluez.Device1` interface defined for all
# objects.
(def ch (ev/chan))
(sdbus/subscribe-properties-changed sys "org.bluez.Device1" ch :sender "org.bluez")

(printf "Listening for new Bluetooth devices... (Ctrl-C to exit)")
(forever
  (match (ev/take ch)
    [:ok msg]
    # The message payload for PropertiesChanged does not contain the
    # object path that triggered the signal, ie the device object
    # name. We can still get it though by inspecting the message
    # header.
    (let [object (sdbus/message-get-path msg)
          @[_ property] (sdbus/message-read msg :all)]
      (when (connected? property)
        (:Notify notifications "" 0 "" "Bluetooth"
                 (string/format "Connected: %s" (device-name sys object))
                 @[] @{} 5000)))
    [:error _]
    (error "Error from signal subscription")))
