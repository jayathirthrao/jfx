/*
 * Copyright (c) 2020, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package pseudoScriptEngineCompilable;

import javax.script.Bindings;
import javax.script.CompiledScript;
import javax.script.ScriptContext;
import javax.script.ScriptEngine;
import javax.script.ScriptException;

public class RgfPseudoCompiledScript extends CompiledScript {
    String code = null;
    ScriptEngine scriptEngine = null;

    RgfPseudoCompiledScript(String code, ScriptEngine scriptEngine) {
        this.code = code;
        this.scriptEngine = scriptEngine;
    }

    @Override
    public Object eval(Bindings bindings) throws ScriptException {
        return scriptEngine.eval("RgfPseudoCompiledScript.eval(Bindings bindings): " + code, bindings);
    }

    @Override
    public Object eval(ScriptContext context) throws ScriptException {
        return scriptEngine.eval("RgfPseudoCompiledScript.eval(ScriptContext context): " + code, context);
    }

    @Override
    public Object eval() throws ScriptException {
        return scriptEngine.eval("RgfPseudoCompiledScript.eval(): " + code );
    }

    @Override
    public ScriptEngine getEngine() {
        return scriptEngine;
    }
}
