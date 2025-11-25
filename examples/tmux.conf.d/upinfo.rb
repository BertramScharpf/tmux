#
#  upinfo.rb  --  Information about uptime and battery
#


module Tmux

  class <<self

    TIME_UNITS = { s: 60, m: 60, h: 24, d: 7, w: nil, }

    def uptime hide: nil, show_zero: nil
      hide = hide.to_sym if hide
      r = []
      t = (File.read "/proc/uptime").to_i
      TIME_UNITS.each { |u,m|
        t, v = m ? (t.divmod m) : [ nil, t]
        r.unshift v, u if !hide && (show_zero || v.nonzero?)
        hide = nil if u == hide
      }
      r.join
    end

    BATTERY_BASE = "/sys/bus/acpi/drivers/battery/PNP0C0A:00/power_supply"

    def battery= bat
      if bat && !bat.empty? then
        @bat = File.join BATTERY_BASE, bat
        @bat_status   = File.join @bat, "status"
        @bat_capacity = File.join @bat, "capacity"
      else
        @bat = @bat_status = @bat_capacity = nil
      end
    end

    def battery
      return unless @bat
      r = ""
      if (File.read @bat_status) =~ /^Charging/i then
        r << "^"
      end
      c = File.read @bat_capacity
      c.rstrip!
      r << c << "%"
    rescue Errno::ENOENT
      "?%"
    end

    def upinfo sep: nil, **kwargs
      a = [ (uptime **kwargs)]
      b = battery
      a.push b if b
      a.join sep||"  "
    end

  end


  if $0 == __FILE__ then
    self.battery = ENV["BAT"]||"BAT"
    puts upinfo
    puts upinfo hide: :s
    puts upinfo hide: "s"
    puts upinfo hide: :m
    puts upinfo hide: :d, show_zero: true
    puts upinfo show_zero: true
    puts upinfo sep: "|", show_zero: true
  end

end
