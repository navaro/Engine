

SECTIONS
{
  .engine BLOCK(4K)   :
  {
		. = ALIGN((32));

    	__engine_action_base__ = .;
         KEEP(*(SORT(.engine.engine_action.*)))
        . = ALIGN(4);
         __engine_action_end__ = .;
         __engine_event_base__ = .;
         KEEP(*(SORT(.engine.engine_event.*)))
        . = ALIGN(4);
         __engine_event_end__ = .;
          __engine_const_base__ = .;
         KEEP(*(SORT(.engine.engine_const.*)))
        . = ALIGN(4);
         __engine_const_end__ = .;
         __engine_cmd_base__ = .;
         KEEP(*(SORT(.engine.engine_cmd.*)))
        . = ALIGN(4);
         __engine_cmd_end__ = .;
  
  }
} 
INSERT AFTER .text;

