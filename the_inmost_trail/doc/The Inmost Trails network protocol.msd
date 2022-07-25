#//# --------------------------------------------------------------------------------------
#//# Created using Sequence Diagram for Mac
#//# https://www.macsequencediagram.com
#//# https://itunes.apple.com/gb/app/sequence-diagram/id1195426709?mt=12
#//# --------------------------------------------------------------------------------------
title "The Inmost Trails network protocol"

participant "Player" as p
participant "Client" as c
participant "Server" as s

loop [while time is not synchronised well enough]

  activate s
  c->s: Ping
  s->c: Pong
  deactivate s
end
c->p: Enable login
p->c: Enter character name
activate s
c->s: RegistrationRequest
s->c: RegistrationResponse
deactivate s

loop [while the client is connected to the server]
	alt
		p->c: Click mouse on land
		c->s: PlayerCmdWalkToPoint
	else
		p->c: Click mouse on item
		c->s: PlayerCmdInteractWithItem
	else
		p->c: Click mouse on enemy
		c->s: PlayerCmdAttack
	else
		activate s
		c->s: Ping
		s->c: Pong
		deactivate s
	end
	activate s
	s->s: """
Change avatar states
according to game logic
"""
	deactivate s
	loop [while there are changed avatars and free buffer space]
		s->c: AvatarState
	end
	activate c
	c->c: """
Change avatar representation
according to AvatarState
"""
   c->p: Render frame
	deactivate c
	
end