# 设计思路
1. 考虑跨平台，跨引擎的使用，尤其是要适配在UE中使用
2. 方案内主要考虑实现脚本形态，脚本转化为节点数据的解析器和一个基本的节点编辑器，节点编辑器后续可能在引擎中实现
3. 要支持多种需求下的节点实现方法，Native节点和Native类型的注入形式应当尽量通用，但是节点的声明形式需要定义，并且注意声明要求可以是导出的方式
4. 节点的基本形式参考https://github.com/MothCocoon/FlowGraph
5. 整体语法应该AI友好

# 脚本形式参考
以下形式作为主要参考
``` cpp
//import 段
import native.d.gs
import other_base_node.gs

//Define 段
let actor_1 = SoftObjectPath("actor_path_1"); //SoftObjectPath是宿主定义的对象，构造函数，import/export text 由宿主定义。可以用来引用引擎内的资产
let actor_2 = SoftObjectPath("actor_path_2");

//声明一个节点，一般来说节点主要在宿主语言中声明，框架需要提供的是声明一个节点所需要实现的必要函数，以Register的形式注入一个结构体即可。以下结构主要示例节点声明需要实现什么
export Node ComposeNode 
{
    //必要逻辑实现，完成节点数据的基本定义
    def OnInit(){
        let out1 = FFlowOutput("FirstOutput");
        let out2 = FFlowOutput("SecondOutput");
        let in_1 = FFlowIn("FlowIn", in_1);//Flowin要绑定一个函数
        let out_value_1 = TOutValue<int>("OutValue");
        let in_value_ = TInValue<Any>("InValue",int(10));
    }
    //实现一个flow_in
    def in_1(FContext context){
        //标准逻辑端，参考Graph中的逻辑端
        //可以Call(out1)来调用1出口
        //可以read in_value_
        //可以set out_value_1
    }

    //允许在on Post Edit函数中修改输入输出的节点
    def OnPostEdit(FPostEditContext post_edit_context){

    }

}

// Graph的定义
Graph graph_name
{
    in in_var_1 : FType; //类型type的输入参数
    out out_var_2 :FType; //类型type的输出参数
    var var_3:FType //类型var_3的参数

    FNodeType native_node{};//节点和节点的初始化列表，如果节点需要初始化的话，大部分时候不需要
    FNodeType compose_node{}//节点名字全局唯一

    //事件和函数略有不同，可以调用外部节点，自带传入对应FEventContext context.
    // 所有的逻辑段实际上是个图标的构造方法
    event event_name(){
        context.start(native_node.in);
        native_node.out(compose_node.in);
        native_node.complete(compose_node.in_2);

        link(native_node.in_value_1,int(1));
        link(native_node.in_value_2,in_var_1);
        link(compose_node.in_value, native_node.out_value);
    }
    //函数可以供外界调用，函数内用的节点只能在函数内声明,FFunctionContext context
    function function_name_1(in value:FType, out value:FType)
    {
        FNodeType native_node_1{};
        //逻辑段通用
        context.start(native_node.in);
        native_node.out(compose_node.in);
        native_node.complete(context.complete);
    }

    //generate用来表述节点的固有属性,用来填写编辑器生成的内容
    generate(){
        
        Comment comment_a{"comment a"};
        global:native_node.position(10,20).comment(comment_a);
        global:compose_node.position(7,8);
     

        Comment comment_b{"comment b"};
        function_name_1:native_node_1.position(999,999);

    }
}
```