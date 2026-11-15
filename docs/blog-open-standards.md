# My air purifier doesn't need a cloud anymore

*Open standards instead of a vendor app and someone else's servers.*

---

I have two Xiaomi air purifiers at home. They're good machines: quiet,
effective, with a decent air-quality sensor. But to control them from my
phone, I had to install Xiaomi's app, create an account, pick a server
region, and let both purifiers stay connected to Xiaomi's cloud around the
clock.

So switching on a fan in my own living room meant sending a request to a
data centre I never chose, run under rules I can't see, and waiting for the
answer. If those servers go offline, the terms change, or the app is
dropped, the "smart" part stops working. Nothing would be wrong with the
purifier itself. It's held hostage by its software.

I wanted the opposite: a device that speaks an **open standard** and works on
my own network, whether or not any company's servers exist.

That standard is **Matter**, the smart-home language Apple, Google, Amazon,
Samsung and hundreds of others agreed on. A Matter device needs no vendor
app, no account and no internet. It works with Apple Home, Google Home or
Home Assistant, and it keeps working whichever of those you use in ten
years.

So I taught my purifiers to speak Matter. I didn't add hardware or solder
anything. I only gave a chip that was already inside them new software.

**The takeaway for anyone buying smart devices:** ask whether it works
*locally*, *without an account*, and *with any app*. Every "yes" means a
device you actually own.

---

## How it works (for the technically curious)

Inside each purifier are two chips. An **STM32** microcontroller runs the
motor, sensors, display and buttons. An **ESP32** Wi-Fi module has a single
job: pass messages between that STM32 and Xiaomi's cloud. The two talk over
a serial line using a simple text protocol that Xiaomi documents for its
partners.

The ESP32 is also the chip that Espressif's open-source Matter SDK targets.
So the plan wrote itself: **leave the purifier's controller untouched, and
replace the Wi-Fi module's firmware** with one that speaks Matter on one
side and Xiaomi's serial protocol on the other.

The result is a new firmware built on ESP-IDF and esp-matter.
It now runs on both a **Mi Air Purifier 3H and a 3C**, from a single image.
At boot it asks the purifier's controller for a property only one of the two
models has, and sets itself up for that model. In Home Assistant the
purifier shows up as a standard Matter Air Purifier:

- power, Auto, Sleep, Low/Medium/High and a fine speed slider (Fan Control);
- PM2.5 and air quality, plus temperature and humidity on the 3H;
- filter life with a reset button (HEPA Filter Monitoring);
- device faults through Matter's standard diagnostics;
- updates over Wi-Fi that roll back on their own if a new version doesn't
  come up properly.

## What real hardware taught me

**Standards have edge cases.** Home Assistant's fan slider reads a different
attribute than you'd expect, so it kept snapping back until the firmware
reported the setting instead of the motor speed. After a restart, the Matter
stack replayed an old fan setting as if someone had just asked for it. Each
was a small fix, but you only find them on a real device.

**Your router is part of the product.** The purifier wouldn't join my Wi-Fi
at first. My FRITZ!Box advertises a WPA3 compatibility mode and then doesn't
follow it during the handshake. The fix was to ignore that hint. No spec
would have told me. Reading disconnect codes late at night did.

**Always keep a way back.** The first step in the guide is a full backup of
the original flash, so Xiaomi's firmware can be restored in minutes. That
backup holds device keys and Wi-Fi passwords, so it never goes near the
repository. Being able to go back is what makes it reasonable to try.

**Stand on shoulders, and say so.** Community teardowns had already mapped
the chips, pins and protocol. Without them this would have taken months.

## The bigger point

Plenty of good hardware ends up in landfill when a vendor's cloud is
switched off. More of it keeps running, but sends every button press through
servers its owner never chose. Neither is a hardware problem. The motor and
the filter don't care where the commands come from.

Open standards move the decision about how long a device stays useful back
to the person who bought it.

👉 Want to try it on your own purifier? The code, flashing guide and backup
instructions are available on request: send me a message.

*Credits: [sourcediver.org](https://sourcediver.org/posts/240218_unclouded_purifier/),
[Flamingo-tech](https://github.com/Flamingo-tech/xiaomi-air-purifier-reverse-engineering),
[jaromeyer/mipurifier-esphome](https://github.com/jaromeyer/mipurifier-esphome),
[dhewg/esphome-miot](https://github.com/dhewg/esphome-miot),
[blakadder/miot](https://github.com/blakadder/miot) and Espressif's
open-source [ESP-IDF](https://github.com/espressif/esp-idf) and
[esp-matter](https://github.com/espressif/esp-matter).*

---

## Short version for the LinkedIn feed

> My air purifiers no longer need a vendor cloud. 🌬️
>
> To control them from my phone, I needed the vendor's app, an account, and a
> permanent connection to their servers. Switching on a fan in my own living
> room went through a data centre I never chose.
>
> So I wrote new firmware for the Wi-Fi chip that's already inside them. They
> now speak **Matter**, the open smart-home standard behind Apple Home,
> Google Home and Home Assistant, and work entirely on my local network. No
> app, no account, no cloud, no new hardware.
>
> Good hardware shouldn't stop being useful because a vendor's servers go
> away. Open standards put that decision back with the owner.
>
> Running on a Mi Air Purifier 3H and 3C. Code available on request: send
> me a message.
>
> #Matter #OpenStandards #HomeAssistant #ESP32 #RightToRepair #SmartHome
