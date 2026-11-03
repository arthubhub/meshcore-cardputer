# Screenshots for the README (en): make -C app/sim readme-images copies them to docs/images/.
lang en
name Raphael
time 1793541600
pos 43.6116 1.3346
contact Bob 1 fav pos 43.6120 1.3420
contact Alice 2 pos 43.6085 1.3300
contact Relais-Colomiers 0 repeater
rxch Public Alice Can anyone hear the Colomiers repeater?
wait 2000
rx Bob Hi! Are you around tonight?
wait 3000
shot readme_en_home

key m
key enter
expect-screen Bob
type Yes, see you at the park at 6pm!
key enter
wait 3000
shot readme_en_chat
key fn+`
key fn+`

key g
gps fix 9
wait 1500
shot readme_en_gps
key b
shot readme_en_compass
key `
key `

tiles build/maptiles
key k
wait 1500
key -
wait 500
shot readme_en_map
key `

key s
shot readme_en_settings
