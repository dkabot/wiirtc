This homebrew program sets the Wii's real time clock to a date and time based on user input.
Currently, it is assumed that adjusting a month moves 30 days and a year moves 365 days, so you may need to tweak the day after changing those.

It's a fork of a program that sets a hardcoded time, which itself is a fork of a program that sets the Wii's clock automatically by connecting to the internet.
This is useful for people with portable Wiis that may have an MX chip, but use VGA or lack bluetooth or Wi-Fi modules. The Wii's system settings cannot be accessed over VGA or without bluetooth, and existing homebrew doesn't allow manually setting the clock.

The Wii cannot be set to a date after 2030 by normal means, and the Unix time system will no longer be able to track time past 2038, but my portable Wii came out in the year 2149. I dont know what will happen if you try to set it to a date like that; probably don't try it.
