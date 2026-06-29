// Scenario 5: Multiple graphs in one file with cross-references

import "ue_core.d.gs";

Graph Utility_ClampAndLog {
    in raw_value : float;
    out clamped : float;

    PrintString logger{};

    event Execute {
        context.start(logger.enter);
        logger.message = raw_value;
    }
}

Graph Utility_FormatMessage {
    in prefix : FString;
    in value : int;
    out formatted : FString;

    PrintString formatter{};

    event Execute {
        context.start(formatter.enter);
        formatter.message = prefix;
    }
}

Graph MainController {
    in input_value : float;
    in label : FString;
    in count : int;

    Utility_ClampAndLog clamp{};
    Utility_FormatMessage fmt{};
    PrintString final_output{};
    Delay wait{};

    event OnStart {
        context.start(clamp.Execute);
        clamp.raw_value = input_value;
    }

    event OnFormat {
        context.start(fmt.Execute);
        fmt.prefix = label;
        fmt.value = count;
    }

    function DoOutput {
        context.start(context.done);
        context.result = input_value;
    }
}
