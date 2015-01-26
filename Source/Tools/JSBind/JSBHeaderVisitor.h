
#pragma once

#include <Atomic/IO/Log.h>
#include <Atomic/Core/ProcessUtils.h>

#include "JSBHeader.h"
#include "JSBModule.h"
#include "JSBindings.h"
#include "JSBClass.h"
#include "JSBFunction.h"

#include "JSBNameVisitor.h"


class JSBHeader;


class JSBHeaderVisitor : public SymbolVisitor
{
    JSBHeader* header_;
    JSBModule* module_;
    JSBindings* bindings_;

    Namespace* globalNamespace_;

public:

    JSBHeaderVisitor(JSBHeader* header, TranslationUnit *unit, Namespace* globalNamespace) :
        header_(header),
        globalNamespace_(globalNamespace)
    {
        module_ = header_->module_;
        bindings_ = module_->bindings_;

        accept(globalNamespace_);

    }

    String getNameString(const Name* name)
    {
        JSBNameVisitor nvisitor;
        return nvisitor(name);
    }

    JSBType* processTypeConversion(Type* type)
    {
        JSBType* jtype = NULL;

        if (type->isIntegerType())
        {
            IntegerType* itype = type->asIntegerType();
            jtype = new JSBPrimitiveType(itype->kind());
        }
        if (type->isFloatType())
        {
            jtype = new JSBPrimitiveType(JSBPrimitiveType::Float);
        }
        else if (type->isNamedType())
        {
            NamedType* ntype = type->asNamedType();
            String classname = getNameString(ntype->name());

            if (classname == "String")
            {
                jtype = new JSBStringType();
            }
            else if (classname == "StringHash")
            {
                jtype = new JSBStringHashType();
            }
            else if (classname == "JS_HEAP_PTR")
            {
                jtype = new JSBHeapPtrType();
            }
            else
            {
                JSBClass* jclass = bindings_->GetClass(classname);
                if (jclass)
                    jtype = new JSBClassType(jclass);
                else
                {
                    // this might be an enum
                    JSBEnum* jenum = bindings_->GetEnum(classname);
                    if (jenum)
                        jtype = new JSBEnumType(jenum);

                }
            }
        }
        else if (type->asUndefinedType())
        {
            UndefinedType* utype = type->asUndefinedType();
            //ErrorExit("Undefined type");
        }

        return jtype;

    }

    JSBFunctionType* processFunctionType(FullySpecifiedType fst)
    {
        JSBType* jtype = NULL;
        Type* type = fst.type();

        bool isPointer = false;
        bool isSharedPtr = false;
        bool isReference = false;

        if (type->isPointerType())
        {
            isPointer=true;
            FullySpecifiedType pfst = type->asPointerType()->elementType();
            type = pfst.type();
        }
        if (type->isReferenceType())
        {
            isReference=true;
            FullySpecifiedType pfst = type->asReferenceType()->elementType();
            type = pfst.type();
        }

        if (fst.isUnsigned())
        {
            if (type->isUndefinedType())
            {
                // this happens when just using "unsigned" in code
                jtype = new JSBPrimitiveType(JSBPrimitiveType::Int, true);
            }
        }


        if (!jtype)
            jtype = processTypeConversion(type);

        if (!jtype)
            return NULL;

        // no pointers to prim atm
        if ((isPointer || isReference) && jtype->asPrimitiveType())
            return NULL;

        JSBFunctionType* ftype = new JSBFunctionType(jtype);
        ftype->isPointer_ = isPointer;
        ftype->isSharedPtr_ = isSharedPtr;
        ftype->isReference_ = isReference;

        return ftype;

    }

    JSBFunctionType* processFunctionArgType(Argument* arg)
    {

        JSBFunctionType* jtype = processFunctionType(arg->type());

        if (!jtype)
            return NULL;

        jtype->name_ = getNameString(arg->name());

        return jtype;

    }


    JSBFunctionType* processFunctionReturnType(Function* function)
    {
        if (!function->hasReturnType())
        {
            return NULL;
        }

        JSBFunctionType* jtype = processFunctionType(function->returnType());

        if (!jtype)
            return NULL;

        return jtype;

    }

    JSBFunction* processFunction(JSBClass* klass, Function* function)
    {
        JSBFunction* jfunction = new JSBFunction(klass);

        // don't ... atm
        if (function->isVariadic())
            return NULL;

        jfunction->name_ = getNameString(function->name());

        // don't support operators atm
        if (jfunction->name_.StartsWith("operator "))
            return NULL;

        if (jfunction->name_ == klass->GetClassName())
            jfunction->isConstructor_ = true;

        if (jfunction->name_.StartsWith("~"))
            jfunction->isDestructor_ = true;

        // see if we support return type
        if (function->hasReturnType() && !function->returnType().type()->isVoidType())
        {            
            jfunction->returnType_ = processFunctionReturnType(function);
            if (!jfunction->returnType_)
                return NULL;
        }

        if (function->hasArguments())
        {
            for (unsigned i = 0; i < function->argumentCount(); i++)
            {
                Symbol* symbol = function->argumentAt(i);
                if (symbol->isArgument())
                {
                    Argument* arg = symbol->asArgument();

                    JSBFunctionType* ftype = processFunctionArgType(arg);
                    if (!ftype)
                        return NULL;

                    if (arg->hasInitializer())
                    {
                        ftype->initializer_ = arg->initializer()->chars();
                    }

                    jfunction->AddParameter(ftype);
                }
                else
                {
                    return NULL;
                }

            }
        }

        return jfunction;

    }

    virtual bool visit(Namespace *nspace)
    {
        String name = getNameString(nspace->name());

        // LOGINFOF("Namespace: %s", name.CString());

        return true;

    }

    // reject template types
    virtual bool visit(Template *t)
    {
        return false;
    }

    // enums visited in preprocessor visitor
    virtual bool visit(Enum *penum)
    {
        return false;
    }

    // global var decl or function
    virtual bool visit(Declaration* decl)
    {
        FullySpecifiedType dtype = decl->type();
        Type* type = dtype.type();

        if (type->isPointerType() || type->isReferenceType())
            return true;

        if (type->asEnumType())
            return true;

        if (!type->asFloatType() && !type->asIntegerType())
            return true;

        module_->RegisterConstant(getNameString(decl->name()).CString());

        return true;

    }

    virtual bool visit(Class *klass)
    {
        String name = getNameString(klass->name());

        JSBClass* jclass = bindings_->GetClass(name);

        if (!jclass)
        {
            return false;
        }

        jclass->SetHeader(header_);
        //LOGINFOF("Adding Class: %s to Module: %s", name.CString(), module_->name_.CString());
        jclass->SetModule(module_);

        module_->AddClass(jclass);

        for (unsigned i = 0; i < klass->baseClassCount(); i++)
        {
            BaseClass* baseclass = klass->baseClassAt(i);
            String baseclassname = getNameString(baseclass->name());

            JSBClass* base = bindings_->GetClass(baseclassname);

            if (!base)
            {
                LOGINFOF("Warning: %s baseclass %s not in bindings", name.CString(), baseclassname.CString());
            }
            else
            {
                jclass->AddBaseClass(base);
            }
        }

        for (unsigned i = 0; i < klass->memberCount(); i++)
        {
            Symbol* symbol = klass->memberAt(i);

            Declaration* decl = symbol->asDeclaration();

            // if the function describes the body in the header
            Function* function = symbol->asFunction();

            // otherwise it could be a decl
            if (!function && decl)
                function = decl->type()->asFunctionType();

            if (function)
            {
                if (function->isPureVirtual())
                    jclass->setAbstract(true);

                // only want public functions
                if (!symbol->isPublic())
                    continue;

                JSBFunction* jfunction = processFunction(jclass, function);
                if (jfunction)
                    jclass->AddFunction(jfunction);
            }

        }
        // return false so we don't traverse the class members
        return false;
    }

};