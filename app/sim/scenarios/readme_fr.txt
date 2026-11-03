# Screenshots for the README (fr): make -C app/sim readme-images copies them to docs/images/.
lang fr
name Raphael
time 1793541600
pos 43.6116 1.3346
contact Bob 1 fav pos 43.6120 1.3420
contact Alice 2 pos 43.6085 1.3300
contact Relais-Colomiers 0 repeater
rxch Public Alice Quelqu'un capte le relais de Colomiers ?
wait 2000
rx Bob Salut ! Tu es dispo ce soir ?
wait 3000
shot readme_fr_home

key m
key enter
expect-screen Bob
type Oui, rendez-vous au parc 
key alt+a
type  18h !
key enter
wait 3000
shot readme_fr_chat
key fn+`
key fn+`

key g
gps fix 9
wait 1500
shot readme_fr_gps
key b
shot readme_fr_compass
key `
key `

tiles build/maptiles
key k
wait 1500
key -
wait 500
shot readme_fr_map
key `

key s
shot readme_fr_settings
