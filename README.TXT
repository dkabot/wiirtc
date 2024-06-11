This homebrew program sets the Wii's real time clock to a date and time based on a (hardcoded, for now) Epoch Unix timestamp. You can then adjust the hour to correct for time zones.

It's a fork of a program that sets the Wii's clock automatically by connecting to the internet.
This is useful for people with portable Wiis that may have an MX chip, but not bluetooth or Wi-Fi modules. The Wii's system settings cannot be accessed without bluetooth, and the RTC value is one of the few things no existing homebrew can modify.

In the future I want this program to read a Unix timestamp from a config file on the Wii's USB drive.

The Wii cannot be set to a date after 2030 by normal means, and the Unix time system will no longer be able to track time past 2038, but my portable Wii came out in the year 2149. I dont know what will happen if you try to set it to a date like that, probably don't try it.