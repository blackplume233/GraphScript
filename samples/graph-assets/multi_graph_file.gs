// Scenario 5: Multiple graphs in one file with cross-references

import "ue_core.d.gs";

graph Utility_ClampAndLog {
    @graph.input
    param raw_value: float;
    @graph.output
    param clamped: float;
    node logger {
        type PrintString;
    }
    event Execute {
        connect(context.start, logger.enter);
        bind(raw_value, logger.message);
    }
}

graph Utility_FormatMessage {
    @graph.input
    param prefix: FString;
    @graph.input
    param value: int;
    @graph.output
    param formatted: FString;
    node formatter {
        type PrintString;
    }
    event Execute {
        connect(context.start, formatter.enter);
        bind(prefix, formatter.message);
    }
}

graph MainController {
    @graph.input
    param input_value: float;
    @graph.input
    param label: FString;
    @graph.input
    param count: int;
    node clamp {
        type Utility_ClampAndLog;
    }
    node fmt {
        type Utility_FormatMessage;
    }
    node final_output {
        type PrintString;
    }
    node wait {
        type Delay;
    }
    event OnStart {
        connect(context.start, clamp.Execute);
        bind(input_value, clamp.raw_value);
    }

    event OnFormat {
        connect(context.start, fmt.Execute);
        bind(label, fmt.prefix);
        bind(count, fmt.value);
    }

    function DoOutput {
        connect(context.start, context.done);
        bind(input_value, context.result);
    }
}
